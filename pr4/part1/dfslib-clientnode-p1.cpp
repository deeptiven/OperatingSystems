#include <regex>
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


#include "dfslib-shared-p1.h"
#include "dfslib-clientnode-p1.h"
#include "proto-src/dfs-service.grpc.pb.h"


#include <google/protobuf/util/time_util.h>


using grpc::Status;
using grpc::Channel;
using grpc::StatusCode;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientContext;

using dfs_service::NoArgs;
using dfs_service::ListOfFiles;
using dfs_service::FileDetails;
using dfs_service::File;
using dfs_service::FileStatus;
using dfs_service::Result;
using dfs_service::FileContents;



using google::protobuf::util::TimeUtil;


using namespace std;


//
// STUDENT INSTRUCTION:
//
// You may want to add aliases to your namespaced service methods here.
// All of the methods will be under the `dfs_service` namespace.
//
// For example, if you have a method named MyMethod, add
// the following:
//
//      using dfs_service::MyMethod
//


DFSClientNodeP1::DFSClientNodeP1() : DFSClientNode() {}

DFSClientNodeP1::~DFSClientNodeP1() noexcept {}

StatusCode DFSClientNodeP1::Store(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to store a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept and store a file.
    //
    // When working with files in gRPC you'll need to stream
    // the file contents, so consider the use of gRPC's ClientWriter.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the client
    // StatusCode::CANCELLED otherwise
    //
    //

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout)); 
    context.AddMetadata("filename", filename);

    FileDetails response;

    //rpc StoreFile(stream FileContents) returns (FileDetails);
    std::unique_ptr<ClientWriter<FileContents>> store_response = service_stub->StoreFile(&context, &response);

    std::string filePath = WrapPath(filename);
    ifstream infile(filePath);
    FileContents content;

    //find filesize
    struct stat statResult;
    if(stat(filePath.c_str(), &statResult) != 0) {
        return StatusCode::NOT_FOUND;
    }
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


StatusCode DFSClientNodeP1::Fetch(const std::string &filename) {



    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to fetch a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept a file request and return the contents
    // of a file from the service.
    //
    // As with the store function, you'll need to stream the
    // contents, so consider the use of gRPC's ClientReader.
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
    request.set_filename(filename);

    std::string filePath = WrapPath(filename);

    std::unique_ptr<ClientReader<FileContents>> response = service_stub->FetchFile(&context, request);

    ofstream outfile;

    FileContents chunck;

    while(response->Read(&chunck)) {
        if(!outfile.is_open()) {
            outfile.open(filePath, ios::trunc);
        }
        const std::string& chunckdata = chunck.chunckcontent();
        outfile << chunckdata;
    }
    outfile.close();
    Status status = response->Finish();

    return status.error_code();
}

StatusCode DFSClientNodeP1::Delete(const std::string& filename) {

    
    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to delete a file here. Refer to the Part 1
    // student instruction for details on the basics.
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
    request.set_filename(filename);

    FileDetails response;

    Status status = service_stub->DeleteFile(&context, request, &response);
    
    return status.error_code();
}

StatusCode DFSClientNodeP1::List(std::map<std::string,int>* file_map, bool display) {


    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to list all files here. This method
    // should connect to your service's list method and return
    // a list of files using the message type you created.
    //
    // The file_map parameter is a simple map of files. You should fill
    // the file_map with the list of files you receive with keys as the
    // file name and values as the modified time (mtime) of the file
    // received from the server.
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

    for(FileDetails details : response.filedetails()) {
        file_map->insert(std::pair<std::string,long>(details.filename(), details.modifiedtime()));
        std::cout << "client: " << details.filename() << details.modifiedtime();
    }
    return status.error_code();
}

StatusCode DFSClientNodeP1::Stat(const std::string &filename, void* file_status) {

    
    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to get the status of a file here. This method should
    // retrieve the status of a file on the server. Note that you won't be
    // tested on this method, but you will most likely find that you need
    // a way to get the status of a file in order to synchronize later.
    //
    // The status might include data such as name, size, mtime, crc, etc.
    //
    // The file_status is left as a void* so that you can use it to pass
    // a message type that you defined. For instance, may want to use that message
    // type after calling Stat from another method.
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
    request.set_filename(filename);

    FileStatus response;

    Status status = service_stub->GetFileStatus(&context, request, &response);

    file_status = &response;
    return status.error_code();
}

//
// STUDENT INSTRUCTION:
//
// Add your additional code here, including
// implementations of your client methods
//

