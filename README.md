# Connectivity monitor

A network connectivity monitor written in C++ 17.

Run the preparation script before building. The main thing it does is cloning [this C++ wrapper around libcURL](https://github.com/jpbarrette/curlpp).

## Usage

```
Use: ./connectivity-monitor <FILENAME> <ACTION> [URL]
     Actions are --dump, --stats, --monitor <URL>.
```
