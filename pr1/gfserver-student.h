/*
 *  This file is for use by students to define anything they wish.  It is used by the gf server implementation
 */
#ifndef __GF_SERVER_STUDENT_H__
#define __GF_SERVER_STUDENT_H__

#include "gf-student.h"
#include "gfserver.h"


struct gfserver_t {
    int max_npending;
    unsigned short port;
    int socketfd;

    gfcontext_t *ctx;
    gfstatus_t status;
    size_t file_len;

    gfh_error_t (*handler)(gfcontext_t **, const char *, void*);
    void* arg;

};

struct gfcontext_t {
    int clientsocketfd;
};

char* is_header_valid(char* header);

ssize_t send_file_contents(int sock_fd, const void *data, size_t len);

#endif // __GF_SERVER_STUDENT_H__