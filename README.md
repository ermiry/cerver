![Cerver Banner](./img/banner.png)

![Cerver Production Integration](https://github.com/ermiry/cerver/workflows/Cerver%20Production%20Integration/badge.svg)
![Cerver Continuous Integration](https://github.com/ermiry/cerver/workflows/Cerver%20Continuous%20Integration/badge.svg)
![Cerver Tests](https://github.com/ermiry/cerver/workflows/Cerver%20Tests/badge.svg)

**Cerver** is a development framework, created by Ermiry, designed to provide you with the necessary tools for creating fast and reliable multipurpose servers written in C/C++. It is highly customizable and with an extensive set of features out of the box, and yet, it is extremely lightweight.

**Cerver** was originally created as a game server, and since then, it has evolved into a framework that can be used as a base to develop any kind of server type you might think of.

**Cerver** was designed from the ground up for fast and easy deployments, with great scalability in mind, without compromising security or reliability, supporting many critical features out of the box that are essential for today applications, like authentication and sessions, load balancer, file operations, and many more.

**Cerver** helps you get started with three different server templates:

**Game Server:** Designed for any kind of game you can think of, like a simple online game connecting two players, to an RPG game with an open world and a combination of on going instances like dungeons (hosted inside lobbies), all updating in real time.

**Http Server:** You can use cerver to host a simple website that only requires static files, or as a support API that can handle I/O opperations for a more complex backend.

**File Server:** You can set up a personal file storage server that you can use to backup your files or that can be used in a production environment.

**Handler types:** a TCP cerver handles its connections in one of two ways, selected with `cerver_set_handler_type ()`:

- **Poll** (`CERVER_HANDLER_TYPE_POLL`): a single thread accepts, receives and runs the direct handlers with `poll ()`. The sockets block when they send, up to `CERVER_DEFAULT_SEND_TIMEOUT` seconds, so a client that does not read, or a slow direct handler, delays every connection; the handlers with their own queues avoid it.
- **Threads** (`CERVER_HANDLER_TYPE_THREADS`): each connection is received in its own thread, a thread of the thpool (`cerver_set_thpool_n_threads ()`) while the connection lives, or a detachable thread. With the thpool, the cerver handles at most as many connections as threads at the same time, and the others wait in the listen backlog or are rejected (`cerver_set_overload_policy ()`). Only this type creates the thpool.

An UDP cerver ignores the handler type: a single thread receives every datagram.

**Cerver** provides you with all the required methods to directly create one of these servers, but you can also take advantage of all the features and combine them to create your own custom server that best fits your needs. You can start without an existing template and then customize the cerver methods and hanlders to best fit your application's needs

We use **Cerver** to power many of our core operations in Ermiry, so it is in constant development and new features are being added all the time. You can check the latest news in our [devblog](https://cerver.ermiry.com/devblog) here!

**Cerver** is now in stable release 1.0 and you can get it right now. It is totally open source and the code is well documented, you can check our official [documentation](https://cerver.ermiry.com/docs) page to help you get started.
