const axios = require ('axios');
const express = require ('express');

const app = express ();

const cerver_url = 'http://192.168.100.39:7010/';

app.get ('/api/test', (req, res) => {

	axios ({
		method: 'get',
		url: cerver_url + "?action=test",
		// headers: { Connection: 'keep-alive' }
	})
		.then (result => {
			console.log (result);
			return res.status (200).json ({ msg: result.data });
		})
		.catch (err => {
			console.error (err);
			return res.status (400).json ({ msg: 'Failed test!'});
		});

});

// catch all
app.get ("*", (req, res) => res.json ({ msg: 'Web Cerver!' }));

const port = process.env.PORT;

app.listen (port, () => console.log (`Server running on port ${port}`));