#include <regex>
#include <mutex>
#include <vector>
#include <string>
#include <thread>
#include <cstdio>
#include <chrono>
#include <errno.h>
#include <csignal>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <getopt.h>
#include <unistd.h>
#include <limits.h>
#include <sys/inotify.h>
#include <grpcpp/grpcpp.h>
#include <utime.h>

#include "src/dfs-utils.h"
#include "src/dfslibx-clientnode-p2.h"
#include "dfslib-shared-p2.h"
#include "dfslib-clientnode-p2.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Channel;
using grpc::StatusCode;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientContext;


extern dfs_log_level_e DFS_LOG_LEVEL;

using dfs_service::NoArgs;
using dfs_service::ListOfFiles;
using dfs_service::FileDetails;
using dfs_service::File;
using dfs_service::FileStatus;
using dfs_service::FileContents;
using dfs_service::WriteLock;

//lock
std::mutex mutex;


//
// STUDENT INSTRUCTION:
//
// Change these "using" aliases to the specific
// message types you are using to indicate
// a file request and a listing of files from the server.
//
using FileRequestType = dfs_service::File;
using FileListResponseType = dfs_service::ListOfFiles;

DFSClientNodeP2::DFSClientNodeP2() : DFSClientNode() {}
DFSClientNodeP2::~DFSClientNodeP2() {}

grpc::StatusCode DFSClientNodeP2::RequestWriteAccess(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to obtain a write lock here when trying to store a file.
    // This method should request a write lock for the given file at the server,
    // so that the current client becomes the sole creator/writer. If the server
    // responds with a RESOURCE_EXHAUSTED response, the client should cancel
    // the current file storage
    //
    // The StatusCode response should be:
    //
    // OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
    // StatusCode::CANCELLED otherwise
    //
    //

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout)); 
    context.AddMetadata("clientid", ClientId()); //adding this to know which client has lock..

    File request;
    //request.set_filename(filename);
    request.set_name(filename);

    WriteLock writeLock;

   Status status = service_stub->GetWriteLock(&context, request, &writeLock);
   if(!status.ok()) {
       return status.error_code();
   }
   return StatusCode::OK;
}

grpc::StatusCode DFSClientNodeP2::Store(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to store a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation. However, you will
    // need to adjust this method to recognize when a file trying to be
    // stored is the same on the server (i.e. the ALREADY_EXISTS gRPC response).
    //
    // You will also need to add a request for a write lock before attempting to store.
    //
    // If the write lock request fails, you should return a status of RESOURCE_EXHAUSTED
    // and cancel the current operation.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::ALREADY_EXISTS - if the local cached file has not changed from the server version
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
    // StatusCode::CANCELLED otherwise
    //
    //
    StatusCode writeLockResult = RequestWriteAccess(filename);
    if(writeLockResult != StatusCode::OK) {
        return StatusCode::RESOURCE_EXHAUSTED;
    }

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout)); 
    context.AddMetadata("filename", filename);
    context.AddMetadata("clientid", ClientId());
    std::string filePath = WrapPath(filename);
    struct stat statResult;
    if(stat(filePath.c_str(), &statResult) != 0) {
        return StatusCode::NOT_FOUND;
    }

    context.AddMetadata("modifiedtime", std::to_string(statResult.st_mtime));
    context.AddMetadata("checksum", std::to_string(dfs_file_checksum(filePath, &this->crc_table)));
 

    FileDetails response;

    //rpc StoreFile(stream FileContents) returns (FileDetails);
    std::unique_ptr<ClientWriter<FileContents>> store_response = service_stub->StoreFile(&context, &response);
    std::ifstream infile(filePath);
    FileContents content;

    //find filesize
       int fileSize = statResult.st_size;

    int sent = 0;
    while(!infile.eof() && sent < fileSize) {
        int bytesToSend;
        int minvalue = fileSize - sent;
        if(minvalue > ChunkSize) {
            bytesToSend = ChunkSize;
        } else {
            bytesToSend = minvalue;
        }

        //create a buffer
        char buffer[ChunkSize];

        //read contents into buffer
        infile.read(buffer, bytesToSend);

        //send the contents
        content.set_chunckcontent(buffer, bytesToSend);
        store_response->Write(content);

        //update the variables
        sent = sent + bytesToSend;
    }
    infile.close();

    if(fileSize != sent) {
        return StatusCode::CANCELLED;
    }
    store_response->WritesDone();
    Status status = store_response->Finish();

    return status.error_code();
}


grpc::StatusCode DFSClientNodeP2::Fetch(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to fetch a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation. However, you will
    // need to adjust this method to recognize when a file trying to be
    // fetched is the same on the client (i.e. the files do not differ
    // between the client and server and a fetch would be unnecessary.
    //
    // The StatusCode response should be:
    //
    // OK - if all went well
    // DEADLINE_EXCEEDED - if the deadline timeout occurs
    // NOT_FOUND - if the file cannot be found on the server
    // ALREADY_EXISTS - if the local cached file has not changed from the server version
    // CANCELLED otherwise
    //
    // Hint: You may want to match the mtime on local files to the server's mtime
    //

    //first check if file already exists on the client
    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout)); 

    std::string filePath = WrapPath(filename);
    context.AddMetadata("checksum", std::to_string(dfs_file_checksum(filePath, &this->crc_table)));


    struct stat statResult;
    if(stat(filePath.c_str(), &statResult) == 0) {
        //if it exists get the modified time
        context.AddMetadata("modifiedtime", std::to_string(statResult.st_mtime));
    }

    File request;
    //request.set_filename(filename);
    request.set_name(filename);

    std::unique_ptr<ClientReader<FileContents>> response = service_stub->FetchFile(&context, request);

    std::ofstream outfile;

    FileContents chunck;

    while(response->Read(&chunck)) {
        if(!outfile.is_open()) {
            outfile.open(filePath, std::ios::trunc);
        }
        const std::string& chunckdata = chunck.chunckcontent();
        outfile << chunckdata;
    }
    outfile.close();
    Status status = response->Finish();

    return status.error_code();
}

grpc::StatusCode DFSClientNodeP2::Delete(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to delete a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You will also need to add a request for a write lock before attempting to delete.
    //
    // If the write lock request fails, you should return a status of RESOURCE_EXHAUSTED
    // and cancel the current operation.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
    // StatusCode::CANCELLED otherwise
    //
    //

    StatusCode writeLockResult = RequestWriteAccess(filename);
    if(writeLockResult != StatusCode::OK) {
        return StatusCode::RESOURCE_EXHAUSTED;
    }

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout)); 
    context.AddMetadata("clientid", ClientId());

    File request;
    //request.set_filename(filename);
    request.set_name(filename);

    FileDetails response;

    Status status = service_stub->DeleteFile(&context, request, &response);
    
    return status.error_code();

}

grpc::StatusCode DFSClientNodeP2::List(std::map<std::string,int>* file_map, bool display) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to list files here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation and add any additional
    // listing details that would be useful to your solution to the list response.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::CANCELLED otherwise
    //
    //

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout)); 

    NoArgs request;
    ListOfFiles response;

    Status status = service_stub->ListFiles(&context, request, &response);

    for(FileStatus details : response.filestatus()) {
        file_map->insert(std::pair<std::string,long>(details.filename(), details.modifiedtime()));
        std::cout << "client: " << details.filename() << details.modifiedtime();
    }
    return status.error_code();
}

grpc::StatusCode DFSClientNodeP2::Stat(const std::string &filename, void* file_status) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to get the status of a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation and add any additional
    // status details that would be useful to your solution.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout)); 

    File request;
    //request.set_filename(filename);
    request.set_name(filename);

    FileStatus response;

    Status status = service_stub->GetFileStatus(&context, request, &response);

    file_status = &response;
    return status.error_code();
}

void DFSClientNodeP2::InotifyWatcherCallback(std::function<void()> callback) {

    //
    // STUDENT INSTRUCTION:
    //
    // This method gets called each time inotify signals a change
    // to a file on the file system. That is every time a file is
    // modified or created.
    //
    // You may want to consider how this section will affect
    // concurrent actions between the inotify watcher and the
    // asynchronous callbacks associated with the server.
    //
    // The callback method shown must be called here, but you may surround it with
    // whatever structures you feel are necessary to ensure proper coordination
    // between the async and watcher threads.
    //
    // Hint: how can you prevent race conditions between this thread and
    // the async thread when a file event has been signaled?
    //

    //this can be synced using locks
    mutex.lock();
    
    callback();

    mutex.unlock();

}

//
// STUDENT INSTRUCTION:
//
// This method handles the gRPC asynchronous callbacks from the server.
// We've provided the base structure for you, but you should review
// the hints provided in the STUDENT INSTRUCTION sections below
// in order to complete this method.
//
void DFSClientNodeP2::HandleCallbackList() {

    void* tag;

    bool ok = false;


    //
    // STUDENT INSTRUCTION:
    //
    // Add your file list synchronization code here.
    //
    // When the server responds to an asynchronous request for the CallbackList,
    // this method is called. You should then synchronize the
    // files between the server and the client based on the goals
    // described in the readme.
    //
    // In addition to synchronizing the files, you'll also need to ensure
    // that the async thread and the file watcher thread are cooperating. These
    // two threads could easily get into a race condition where both are trying
    // to write or fetch over top of each other. So, you'll need to determine
    // what type of locking/guarding is necessary to ensure the threads are
    // properly coordinated.
    //

    // Block until the next result is available in the completion queue.
    while (completion_queue.Next(&tag, &ok)) {
        {
            //
            // STUDENT INSTRUCTION:
            //
            // Consider adding a critical section or RAII style lock here
            //

            // The tag is the memory location of the call_data object
            AsyncClientData<FileListResponseType> *call_data = static_cast<AsyncClientData<FileListResponseType> *>(tag);

            dfs_log(LL_DEBUG2) << "Received completion queue callback";

            // Verify that the request was completed successfully. Note that "ok"
            // corresponds solely to the request for updates introduced by Finish().
            // GPR_ASSERT(ok);
            if (!ok) {
                dfs_log(LL_ERROR) << "Completion queue callback not ok.";
            }

            if (ok && call_data->status.ok()) {

                dfs_log(LL_DEBUG3) << "Handling async callback ";

                //
                // STUDENT INSTRUCTION:
                //
                // Add your handling of the asynchronous event calls here.
                // For example, based on the file listing returned from the server,
                // how should the client respond to this updated information?
                // Should it retrieve an updated version of the file?
                // Send an update to the server?
                // Do nothing?
                //

                mutex.lock();
                for(const FileStatus& status : call_data->reply.filestatus()) {

                    std::string filename = status.filename();
                    std::string filePath = WrapPath(filename);

                    //if file is not present in client, fetch from server
                    struct stat statResult;
                    if(stat(filePath.c_str(), &statResult) != 0) {
                        if(this->Fetch(filename) != StatusCode::OK) {
                            dfs_log(LL_ERROR) << "Couldn't fetch file from server as file is not present in client";
                        }
                    }

                    //if server has more recent version
                    else if(status.modifiedtime() > statResult.st_mtime) {
                        if(this->Fetch(filename) != StatusCode::OK) {
                            dfs_log(LL_ERROR) << "Couldn't fetch file from server as server has newer version";
                        } 
                        if(this->Fetch(filename) == StatusCode::ALREADY_EXISTS) {
                            //update the new modified time
                            int modifiedTime = status.modifiedtime();
                            //statResult.st_mtime = modifiedTime;
                            struct utimbuf new_time;
                            new_time.modtime = modifiedTime;
                            utime(filePath.c_str(), &new_time);
                        }
                    }

                    //if client has most recent file
                    else if(status.modifiedtime() < statResult.st_mtime) {
                        if(this->Store(filename) != StatusCode::OK) {
                            dfs_log(LL_ERROR) << "Couldn't store file in server";
                        }
                    } else if(status.size() == 0) {
                        int rem = remove(filePath.c_str());
                        if(rem != 0) {
                            dfs_log(LL_ERROR) << "Couldn't delete file in client";
                        }
                    }
                
            }
                mutex.unlock(); 
            } else {
                dfs_log(LL_ERROR) << "Status was not ok. Will try again in " << DFS_RESET_TIMEOUT << " milliseconds.";
                dfs_log(LL_ERROR) << call_data->status.error_message();
                std::this_thread::sleep_for(std::chrono::milliseconds(DFS_RESET_TIMEOUT));
            }

            // Once we're complete, deallocate the call_data object.
            delete call_data;

            //
            // STUDENT INSTRUCTION:
            //
            // Add any additional syncing/locking mechanisms you may need here

        }


        // Start the process over and wait for the next callback response
        dfs_log(LL_DEBUG3) << "Calling InitCallbackList";
        InitCallbackList();

    }
}

/**
 * This method will start the callback request to the server, requesting
 * an update whenever the server sees that files have been modified.
 *
 * We're making use of a template function here, so that we can keep some
 * of the more intricate workings of the async process out of the way, and
 * give you a chance to focus more on the project's requirements.
 */
void DFSClientNodeP2::InitCallbackList() {
    CallbackList<FileRequestType, FileListResponseType>();
}

//
// STUDENT INSTRUCTION:
//
// Add any additional code you need to here
//


