// A small thread-safe queue written in C
// 2011 by Tobias Thiel

#include "cerver/collections/queue.h"

int8_t queue_flush_internal(queue_t *q, uint8_t fd, void (*ff)(void *));

#pragma region private

int8_t queue_lock_internal(queue_t *q) {
	if (q == NULL)
		return Q_ERR_INVALID;
	// all errors are unrecoverable for us
	if(0 != pthread_mutex_lock(q->mutex))
		return Q_ERR_LOCK;
	return Q_OK;
}

int8_t queue_unlock_internal(queue_t *q) {
	if (q == NULL)
		return Q_ERR_INVALID;
	// all errors are unrecoverable for us
	if(0 != pthread_mutex_unlock(q->mutex))
		return Q_ERR_LOCK;
	return Q_OK;
}

int8_t queue_destroy_internal(queue_t *q, uint8_t fd, void (*ff)(void *)) {
	// this method will not immediately return on error,
	// it will try to release all the memory that was allocated.
	int error = Q_OK;
	// make sure no new data comes and wake all waiting threads
	error = queue_set_new_data(q, 0);
	error = queue_lock_internal(q);
	
	// release internal element memory
	error = queue_flush_internal(q, fd, ff);
	
	// destroy lock and queue etc
	error = pthread_cond_destroy(q->cond_get);
	free(q->cond_get);
	error = pthread_cond_destroy(q->cond_put);
	free(q->cond_put);
	
	error = queue_unlock_internal(q);
	while(EBUSY == (error = pthread_mutex_destroy(q->mutex)))
		sleepmilli(100);
	free(q->mutex);
	
	// destroy queue
	free(q);
	return error;
}

int8_t queue_flush_internal(queue_t *q, uint8_t fd, void (*ff)(void *)) {
	if(q == NULL)
		return Q_ERR_INVALID;
	
	queue_element_t *qe = q->first_el;
	queue_element_t *nqe = NULL;
	while(qe != NULL) {
		nqe = qe->next;
		if(fd != 0 && ff == NULL) {
			free(qe->data);
		} else if(fd != 0 && ff != NULL) {
			ff(qe->data);
		}
		free(qe);
		qe = nqe;
	}
	q->first_el = NULL;
	q->last_el = NULL;
	q->num_els = 0;
	
	return Q_OK;
}

int8_t queue_put_internal(queue_t *q , void *el, int (*action)(pthread_cond_t *, pthread_mutex_t *)) {
	if(q == NULL) // queue not valid
		return Q_ERR_INVALID;
		
	if(q->new_data == 0) { // no new data allowed
		return Q_ERR_NONEWDATA;
	}
	
	// max_elements already reached?
	// if condition _needs_ to be in sync with while loop below!
	if(q->num_els == (UINTX_MAX - 1) || (q->max_els != 0 && q->num_els == q->max_els)) {
		if(action == NULL) {
			return Q_ERR_NUM_ELEMENTS;
		} else {
			while ((q->num_els == (UINTX_MAX - 1) || (q->max_els != 0 && q->num_els == q->max_els)) && q->new_data != 0)
				action(q->cond_put, q->mutex);
			if(q->new_data == 0) {
				return Q_ERR_NONEWDATA;
			}
		}
	}
	
	queue_element_t *new_el = (queue_element_t *)malloc(sizeof(queue_element_t));
	if(new_el == NULL) { // could not allocate memory for new elements
		return Q_ERR_MEM;
	}
	new_el->data = el;
	new_el->next = NULL;
	
	if(q->sort == 0 || q->first_el == NULL) {
		// insert at the end when we don't want to sort or the queue is empty
		if(q->last_el == NULL)
			q->first_el = new_el;
		else
			q->last_el->next = new_el;
		q->last_el = new_el;
	} else {
		// search appropriate place to sort element in
		queue_element_t *s = q->first_el; // s != NULL, because of if condition above
		queue_element_t *t = NULL;
		int asc_first_el = q->asc_order != 0 && q->cmp_el(s->data, el) >= 0;
		int desc_first_el = q->asc_order == 0 && q->cmp_el(s->data, el) <= 0;
		
		if(asc_first_el == 0 && desc_first_el == 0) {
			// element will be inserted between s and t
			for(s = q->first_el, t = s->next; s != NULL && t != NULL; s = t, t = t->next) {
				if(q->asc_order != 0 && q->cmp_el(s->data, el) <= 0 && q->cmp_el(el, t->data) <= 0) {
					// asc: s <= e <= t
					break;
				} else if(q->asc_order == 0 && q->cmp_el(s->data, el) >= 0 && q->cmp_el(el, t->data) >= 0) {
					// desc: s >= e >= t
					break;
				}
			}
			// actually insert
			s->next = new_el;
			new_el->next = t;
			if(t == NULL)
				q->last_el = new_el;
		} else if(asc_first_el != 0 || desc_first_el != 0) {
			// add at front
			new_el->next = q->first_el;
			q->first_el = new_el;
		}
	}
	q->num_els++;
	// notify only one waiting thread, so that we don't have to check and fall to sleep because we were to slow
	pthread_cond_signal(q->cond_get);
	
	return Q_OK;
}

int8_t queue_get_internal(queue_t *q, void **e, int (*action)(pthread_cond_t *, pthread_mutex_t *), int (*cmp)(void *, void *), void *cmpel) {
	if(q == NULL) { // queue not valid
		*e = NULL;
		return Q_ERR_INVALID;
	}
	
	// are elements in the queue?
	if(q->num_els == 0) {
		if(action == NULL) {
			*e = NULL;
			return Q_ERR_NUM_ELEMENTS;
		} else {
			while(q->num_els == 0 && q->new_data != 0)
				action(q->cond_get, q->mutex);
			if (q->num_els == 0 && q->new_data == 0)
				return Q_ERR_NONEWDATA;
		}
	}
	
	// get first element (which fulfills the requirements)
	queue_element_t *el_prev = NULL, *el = q->first_el;
	while(cmp != NULL && el != NULL && 0 != cmp(el, cmpel)) {
		el_prev = el;
		el = el->next;
	}
	
	if(el != NULL && el_prev == NULL) {
		// first element is removed
		q->first_el = el->next;
		if(q->first_el == NULL)
			q->last_el = NULL;
		q->num_els--;
		*e = el->data;
		free(el);
	} else if(el != NULL && el_prev != NULL) {
		// element in the middle is removed
		el_prev->next = el->next;
		q->num_els--;
		*e = el->data;
		free(el);
	} else {
		// element is invalid
		*e = NULL;
		return Q_ERR_INVALID_ELEMENT;
	}
	
	// notify only one waiting thread
	pthread_cond_signal(q->cond_put);

	return Q_OK;
}

#pragma endregion

queue_t *queue_create(void) {
	queue_t *q = (queue_t *)malloc(sizeof(queue_t));
	if(q != NULL) {
		q->mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
		if(q->mutex == NULL) {
			free(q);
			return NULL;
		}
		pthread_mutex_init(q->mutex, NULL);
		
		q->cond_get = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
		if(q->cond_get == NULL) {
			pthread_mutex_destroy(q->mutex);
			free(q->mutex);
			free(q);
			return NULL;
		}
		pthread_cond_init(q->cond_get, NULL);

		q->cond_put = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
		if(q->cond_put == NULL) {
			pthread_cond_destroy(q->cond_get);
			free(q->cond_get);
			pthread_mutex_destroy(q->mutex);
			free(q->mutex);
			free(q);
			return NULL;
		}
		pthread_cond_init(q->cond_put, NULL);
		
		q->first_el = NULL;
		q->last_el = NULL;
		q->num_els = 0;
		q->max_els = 0;
		q->new_data = 1;
		q->sort = 0;
		q->asc_order = 1;
		q->cmp_el = NULL;
	}
	
	return q;
}

queue_t *queue_create_limited(uintX_t max_elements) {
	queue_t *q = queue_create();
	if(q != NULL)
		q->max_els = max_elements;
	
	return q;
}

queue_t *queue_create_sorted(int8_t asc, int (*cmp)(void *, void *)) {
	if(cmp == NULL)
		return NULL;
		
	queue_t *q = queue_create();
	if(q != NULL) {
		q->sort = 1;
		q->asc_order = asc;
		q->cmp_el = cmp;
	}
	
	return q;
}

queue_t *queue_create_limited_sorted(uintX_t max_elements, int8_t asc, int (*cmp)(void *, void *)) {
	if(cmp == NULL)
		return NULL;
		
	queue_t *q = queue_create();
	if(q != NULL) {
		q->max_els = max_elements;
		q->sort = 1;
		q->asc_order = asc;
		q->cmp_el = cmp;
	}
	
	return q;
}

int8_t queue_destroy(queue_t *q) {
	if(q == NULL)
		return Q_ERR_INVALID;
	return queue_destroy_internal(q, 0, NULL);
}

int8_t queue_destroy_complete(queue_t *q, void (*ff)(void *)) {
	if(q == NULL)
		return Q_ERR_INVALID;
	return queue_destroy_internal(q, 1, ff);
}

int8_t queue_flush(queue_t *q) {
	if(q == NULL)
		return Q_ERR_INVALID;
	if (0 != queue_lock_internal(q))
		return Q_ERR_LOCK;

	int8_t r = queue_flush_internal(q, 0, NULL);

	if (0 != queue_unlock_internal(q))
		return Q_ERR_LOCK;
	return r;
}

int8_t queue_flush_complete(queue_t *q, void (*ff)(void *)) {
	if(q == NULL)
		return Q_ERR_INVALID;
	if (0 != queue_lock_internal(q))
		return Q_ERR_LOCK;

	int8_t r = queue_flush_internal(q, 1, ff);

	if (0 != queue_unlock_internal(q))
		return Q_ERR_LOCK;
	return r;
}

uintX_t queue_elements(queue_t *q) {
	uintX_t ret = UINTX_MAX;
	if(q == NULL)
		return ret;
	if (0 != queue_lock_internal(q))
		return ret;

	ret = q->num_els;

	if (0 != queue_unlock_internal(q))
		return Q_ERR_LOCK;
	return ret;
}

int8_t queue_empty(queue_t *q) {
	if(q == NULL)
		return Q_ERR_INVALID;
	if (0 != queue_lock_internal(q))
		return Q_ERR_LOCK;

	uint8_t ret;
	if(q->first_el == NULL || q->last_el == NULL)
		ret = 1;
	else
		ret = 0;
	
	if (0 != queue_unlock_internal(q))
		return Q_ERR_LOCK;
	return ret;
}

int8_t queue_set_new_data(queue_t *q, uint8_t v) {
	if(q == NULL)
		return Q_ERR_INVALID;
	if (0 != queue_lock_internal(q))
		return Q_ERR_LOCK;
	q->new_data = v;
	if (0 != queue_unlock_internal(q))
		return Q_ERR_LOCK;

	if(q->new_data == 0) {
		// notify waiting threads, when new data isn't accepted
		pthread_cond_broadcast(q->cond_get);
		pthread_cond_broadcast(q->cond_put);
	}

	return Q_OK;
}

uint8_t queue_get_new_data(queue_t *q) {
	if(q == NULL)
		return 0;
	if (0 != queue_lock_internal(q))
		return 0;

	uint8_t r = q->new_data;

	if (0 != queue_unlock_internal(q))
		return 0;
	return r;
}

int8_t queue_put(queue_t *q, void *el) {
	if(q == NULL)
		return Q_ERR_INVALID;
	if (0 != queue_lock_internal(q))
		return Q_ERR_LOCK;
	
	int8_t r = queue_put_internal(q, el, NULL);

	if (0 != queue_unlock_internal(q))
		return Q_ERR_LOCK;
	return r;
}

int8_t queue_put_wait(queue_t *q, void *el) {
	if(q == NULL)
		return Q_ERR_INVALID;
	if (0 != queue_lock_internal(q))
		return Q_ERR_LOCK;
	
	int8_t r = queue_put_internal(q, el, pthread_cond_wait);

	if (0 != queue_unlock_internal(q))
		return Q_ERR_LOCK;
	return r;
}

int8_t queue_get(queue_t *q, void **e) {
	*e = NULL;
	if(q == NULL)
		return Q_ERR_INVALID;
	if (0 != queue_lock_internal(q))
		return Q_ERR_LOCK;
	
	int8_t r = queue_get_internal(q, e, NULL, NULL, NULL);

	if (0 != queue_unlock_internal(q))
		return Q_ERR_LOCK;
	return r;
}

int8_t queue_get_wait(queue_t *q, void **e) {
	*e = NULL;
	if(q == NULL)
		return Q_ERR_INVALID;
	if (0 != queue_lock_internal(q))
		return Q_ERR_LOCK;
	
	int8_t r = queue_get_internal(q, e, pthread_cond_wait, NULL, NULL);

	if (0 != queue_unlock_internal(q))
		return Q_ERR_LOCK;
	return r;
}

int8_t queue_get_filtered(queue_t *q, void **e, int (*cmp)(void *, void *), void *cmpel) {
	*e = NULL;
	if(q == NULL)
		return Q_ERR_INVALID;
	if (0 != queue_lock_internal(q))
		return Q_ERR_LOCK;
	
	int8_t r = queue_get_internal(q, e, NULL, cmp, cmpel);

	if (0 != queue_unlock_internal(q))
		return Q_ERR_LOCK;
	return r;
}

int8_t queue_flush_put(queue_t *q, void (*ff)(void *), void *e) {
	if(q == NULL)
		return Q_ERR_INVALID;
	if (0 != queue_lock_internal(q))
		return Q_ERR_LOCK;
	
	int8_t r = queue_flush_internal(q, 0, NULL);
	r = queue_put_internal(q, e, NULL);

	if (0 != queue_unlock_internal(q))
		return Q_ERR_LOCK;
	return r;
}

int8_t queue_flush_complete_put(queue_t *q, void (*ff)(void *), void *e) {
	if(q == NULL)
		return Q_ERR_INVALID;
	if (0 != queue_lock_internal(q))
		return Q_ERR_LOCK;
	
	int8_t r = queue_flush_internal(q, 1, ff);
	r = queue_put_internal(q, e, NULL);

	if (0 != queue_unlock_internal(q))
		return Q_ERR_LOCK;
	return r;
}
