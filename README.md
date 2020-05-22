# Connectivity monitor

[![Build Status](https://travis-ci.org/bernardosulzbach/connectivity-monitor.svg?branch=master)](https://travis-ci.org/bernardosulzbach/connectivity-monitor)

A network connectivity monitor written in C++ 17.

Run the preparation script before building. The main thing it does is cloning [this C++ wrapper around libcURL](https://github.com/jpbarrette/curlpp).

The monitoring is done through periodic HTTP requests to a user-specified endpoint.

Ping and similar diagnostic tools are not use because the network configuration may treat those protocols in a different way than it would treat HTTP requests, and we are not interested in learning whether we can ping an endpoint.

## Usage

```
Use: ./connectivity-monitor <FILENAME> <ACTION> [URL]
     Actions are --dump, --stats, --monitor <URL>.
```

## Log format

The file format is a series of lines, each line containing information about one request.

```
2020-05-21T10:11:30Z 200 300017
2020-05-21T10:12:00Z 200 308442
2020-05-21T10:12:30Z 200 300411
2020-05-21T10:13:00Z 200 297926
2020-05-21T10:13:30Z 200 298180
2020-05-21T10:14:00Z 200 292135
```

Each line has a ISO 8601 timestamp (as 20 UTF-8 characters), optionally followed by the HTTP response code.
If the response code is present, it may be followed by the number of microseconds the request took.

## Supported platforms

This program should work on any modern OS and architecture.
However, it is only tested on Ubuntu Bionic (18.04, x64) using Clang 7.0.0 and GCC 7.4.0.
Please file an issue if it is not working in your platform.

## License

This program is licensed under the BSD 3-Clause License. See the provided LICENSE file for more details.
