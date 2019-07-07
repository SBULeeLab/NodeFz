
Node.fz
=====

This repository contains a modified version of the popular [Node.js](https://github.com/nodejs/node) JavaScript runtime framework, version 0.12.7.
The modifications are exclusively (I think) to the libuv subsystem, which fundamentally provides Node.js's event-driven nature.
Their purpose is to increase the non-determinism already inherent in Node.js's asynchronous, event-driven nature.
The goal of increasing the non-determinism is to flush out *race conditions* in the Node.js application being executed.

The changes I made to Node.js are detailed in the associated [paper](http://dl.acm.org/citation.cfm?id=3064188).
Essentially, I probabilistically re-order the event queue, constrained by legal or practical ordering guarantees prescribed by the Node.js and libuv documentation.

## Does it work?

As described in the associated [paper](http://dl.acm.org/citation.cfm?id=3064188), Node.fz can:
- reproduce race conditions more effectively than Node.js
- increase the schedule space explored by a test suite

Node.fz will make a great addition to your testing methodology, continuous integration pipelines, etc.
Once you've worked out obvious bugs in your program, replace the installation of Node.js with Node.fz, and then re-run your test suite to look for race conditions.

The fuzzing benchmark applications used in the paper are available [here](https://drive.google.com/open?id=1M0TaL4m8v9z-MFf0U1k13OGuKF-evpzz). See the README within it for details. Warning: the tarball is (quite unnecessarily) 200MB and expands to 800MB.

## How do I use it?

Follow the standard Node.js compilation procedure, as described elsewhere in this repository.
Then replace the version of Node.js installed on your machine with the resulting binary (e.g. with `make install`).

Node.fz's behavior can be controlled using the following environment variables:

| Parameter                            | Recommended value        |
| -----------------------------------  | :----------------------: |
| UV_THREADPOOL_SIZE\*                 |  1                       |
| UV_SCHEDULER_TYPE\*                  |  "TP_FREEDOM"            |
| UV_SCHEDULER_MODE\*                  |  "RECORD"                |
| UV_SILENT\*                          |  "1"                     |
| UV_SCHEDULER_TP_DEG_FREEDOM          |  5                       |
| UV_SCHEDULER_TP_MAX_DELAY            |  100                     |
| UV_SCHEDULER_TP_EPOLL_THRESHOLD      |  100                     |
| UV_SCHEDULER_TIMER_LATE_EXEC_TPERC   |  250                     |
| UV_SCHEDULER_IOPOLL_DEG_FREEDOM      |  -1                      |
| UV_SCHEDULER_IOPOLL_DEFER_PERC       |  10                      |
| UV_SCHEDULER_RUN_CLOSING_DEFER_PERC  |  5                       |

\*Feel free to tune all but these parameters.

See deps/uv/src/uv-common.c for an in-depth explanation of each parameter.

## Limitations

Node.fz is a dynamic test tool. It is only as good as the test suite being used to drive it.
If there's a bug in function *B* but your test suite never calls this function, obviously Node.fz won't help you.

## Contributing

The initial prototype of Node.fz was implemented by [James (Jamie) Davis](https://github.com/davisjam).

Please reach out to me to discuss any pull requests you'd like to submit.
The most needed change is porting it up to a more recent version of Node.js (libuv).
I think this should be pretty straightforward.

## Citing this work

If you use this work, please give credit to the associated [paper](http://dl.acm.org/citation.cfm?id=3064188), e.g.

*Davis, James, Arun Thekumparampil, and Dongyoon Lee. "Node. fz: Fuzzing the Server-Side Event-Driven Architecture." Proceedings of the Twelfth European Conference on Computer Systems. ACM, 2017.*
