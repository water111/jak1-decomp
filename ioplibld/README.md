# ioplibld

This tool generates the stubs for external functions that are require when linking an IOP module object.

It takes your objects and the `.ilb` files of the libraries you use and produces stub functions to link into the final object.

This isn't a full `ioplibld` clone, just the minimal features needed for the command below:

```text
ioplibld -s imports.s object.o... : imports.ilb...
```

