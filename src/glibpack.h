#ifndef GLIBPACK_H
#define GLIBPACK_H

// unpacks the instrument library tarfile into base/instruments (once per
// version and archive size); libpath gets that folder. 0 = failed / no archive
int libpack_unpack(const char *tarfile, const char *base, const char *version, char *libpath, int libpathsize);

#endif
