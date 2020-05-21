# Connectivity monitor

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

Each line has a ISO 8601 timestamp (as 20 UTF-8 characters), optionally followed by the HTTP response code.
If the response code is present, it may be followed by the number of microseconds the request took.
