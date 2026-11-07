#ifndef GMIDISYS_H
#define GMIDISYS_H

#define MIDISYS_MAXDEV 32
#define MIDISYS_NAMELEN 128

// only: NULL = list the devices but listen to none, "" = all, else one name
int midisys_open(const char *only);
void midisys_close(void);
int midisys_scan(void);                 // 1 = the device list changed
void midisys_poll(void);
int midisys_read(unsigned char *msg);   // status, data1, data2, device
void midisys_push(int st, int d1, int d2);
int midisys_numdev(void);
const char *midisys_name(int i);
int midisys_used(int i);
// raw data as delivered by the system, before parsing (diagnosis)
unsigned midisys_rawcount(void);
unsigned midisys_rawdump(unsigned from, void (*out)(int dev, const unsigned char *data, int len, void *ctx), void *ctx);
int midisys_maclayout(void);            // macOS: offset of the first packet (4 or 8, 0 = no data yet)

#endif
