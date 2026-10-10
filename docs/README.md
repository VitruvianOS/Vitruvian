# Developer Documentation

To build the developer documentation you will need [Doxygen](https://www.doxygen.nl/) v1.10 or higher.

## Build and Serve

```bash
doxygen Doxyfile
python3 -m http.server 8080 --bind 127.0.0.1 --directory docs/out/html
```

If it looks odd or the build breaks, check your Doxygen version with:

```bash
doxygen -v
```
