/*
 *  This file is for use by students to define anything they wish.  It is used by the gf client implementation
 */
 #ifndef __GF_CLIENT_STUDENT_H__
 #define __GF_CLIENT_STUDENT_H__


 
 #include "gfclient.h"
 #include "gf-student.h"

 #define BUFSIZE 64000

 struct gfcrequest_t {
    size_t bytesreceived;
    size_t filelen;
    gfstatus_t status;
    unsigned short port;
    const char* server;
    const char* path;
    int socketfd;

    void *headerarg;
    void (*headerfunc)(void*, size_t, void *);
    
    void *writearg;
    void (*writefunc)(void*, size_t, void *);


};

/**
 * Process the header from the response and extract the first 
 * chunk of the file if response is OK
 */
int process_header(gfcrequest_t **gfr);

/**
 * Fetch the rest of the file contents after processig the
 * response header
 */
int fetch_file_contents(gfcrequest_t **gfr);
 
 #endif // __GF_CLIENT_STUDENT_H__