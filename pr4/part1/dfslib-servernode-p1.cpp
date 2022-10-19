#include <map>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <grpcpp/grpcpp.h>
#include <google/protobuf/util/time_util.h>
#include <sys/types.h>

#include "src/dfs-utils.h"
#include "dfslib-shared-p1.h"
#include "dfslib-servernode-p1.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Server;
using grpc::StatusCode;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::ServerContext;
using grpc::ServerBuilder;

using dfs_service::DFSService;

using dfs_service::NoArgs;
using dfs_service::ListOfFiles;
using dfs_service::FileDetails;
using dfs_service::File;
using dfs_service::FileStatus;
using dfs_service::Result;
using dfs_service::FileContents;

using google::protobuf::Timestamp;
using google::protobuf::util::TimeUtil;



//
// STUDENT INSTRUCTION:
//
// DFSServiceImpl is the implementation service for the rpc methods
// and message types you defined in the `dfs-service.proto` file.
//
// You should add your definition overrides here for the specific
// methods that you created for your GRPC service protocol. The
// gRPC tutorial described in the readme is a good place to get started
// when trying to understand how to implement this class.
//
// The method signatures generated can be found in `proto-src/dfs-service.grpc.pb.h` file.
//
// Look for the following section:
//
//      class Service : public ::grpc::Service {
//
// The methods returning grpc::Status are the methods you'll want to override.
//
// In C++, you'll want to use the `override` directive as well. For example,
// if you have a service method named MyMethod that takes a MyMessageType
// and a ServerWriter, you'll want to override it similar to the following:
//
//      Status MyMethod(ServerContext* context,
//                      const MyMessageType* request,
//                      ServerWriter<MySegmentType> *writer) override {
//
//          /** code implementation here **/
//      }
//
class DFSServiceImpl final : public DFSService::Service {

private:

    /** The mount path for the server **/
    std::string mount_path;

    /**
     * Prepend the mount path to the filename.
     *
     * @param filepath
     * @return
     */
    const std::string WrapPath(const std::string &filepath) {
        return this->mount_path + filepath;
    }


public:

    DFSServiceImpl(const std::string &mount_path): mount_path(mount_path) {
    }

    ~DFSServiceImpl() {}

    //
    // STUDENT INSTRUCTION:
    //
    // Add your additional code here, including
    // implementations of your protocol service methods
    //

    //rpc StoreFile(stream FileContents) returns (Result);
    Status StoreFile(ServerContext* context,
                      ServerReader<FileContents>* reader,
                      FileDetails* response) override {

    

    //get filename

    std::multimap<grpc::string_ref, grpc::string_ref>::const_iterator metadata_iterator = context->client_metadata().find("filename");
        if(metadata_iterator == context->client_metadata().end())
        {
            return Status(StatusCode::CANCELLED, "Cannot get filename from client");
        }
        std::string fileName = std::string(metadata_iterator->second.begin(),metadata_iterator->second.end());

     std::string filePath = WrapPath(fileName);

    //write to file
    std::ofstream outfile;
    //outfile.open(filePath, std::ios::trunc);

    FileContents chunck;

    while(reader->Read(&chunck)) {
        if (context->IsCancelled()) {       
        return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded"); 
        }   
        if(!outfile.is_open()) {
            outfile.open(filePath, std::ios::trunc);
        }
        const std::string& chunckdata = chunck.chunckcontent();
        outfile << chunckdata;
    }
    outfile.close();
    
    //set the response values
    struct stat statResult;
    if(stat(filePath.c_str(), &statResult) != 0) {
        return Status(StatusCode::NOT_FOUND, "File not found");
    }
    response->set_filename(fileName);
    response->set_modifiedtime(statResult.st_mtime);
    return Status::OK;
    }





    //rpc FetchFile(File) returns (stream FileContents) ;
    Status FetchFile(ServerContext* context,
                     const File* request,
                      ServerWriter<FileContents>* writer) override {

   

    //std::string filePath = "/vagrant/pr4/mnt/server/sample-files/gt-einstein2.jpg";
    std::string filePath = WrapPath(request->filename());

    //first have to check if file is present or not.
    struct stat statResult;
    if(stat(filePath.c_str(), &statResult) != 0) {
        return Status(StatusCode::NOT_FOUND, "File not found");
    }

    //if file is present, get the size
    int fileSize = statResult.st_size;
    int bytesTransferred = 0;
    FileContents content;

    std::ifstream ifile(filePath);

    while(!ifile.eof() && bytesTransferred < fileSize) {
        //we have to transfer in chunks
         if (context->IsCancelled()) {       
        return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
        }
        int bytesToSend;
        int minvalue = fileSize - bytesTransferred;
        if(minvalue > ChunkSize) {
            bytesToSend = ChunkSize;
        } else {
            bytesToSend = minvalue;
        }

        //create a buffer
        char buffer[ChunkSize];
        
        //read contents to buffer
        ifile.read(buffer, bytesToSend);

        //send the contents
        content.set_chunckcontent(buffer, bytesToSend);
        writer->Write(content);

        //update the variables
        bytesTransferred = bytesTransferred + bytesToSend;
    }
    ifile.close();
    

    if(fileSize != bytesTransferred) {
        return Status(StatusCode::CANCELLED, "not fully transferred");
    }
    return Status::OK;
    }



    Status DeleteFile(ServerContext* context,
                      const File* request,
                      FileDetails* response) override {

    

    std::string filePath = WrapPath(request->filename());
    //std::string filePath = "/vagrant/pr4/mnt/server/sample-files/testfile";
    struct stat statResult;
    if(stat(filePath.c_str(), &statResult) != 0) {
        return Status(StatusCode::NOT_FOUND, "File not found");
    }

    if (context->IsCancelled()) {       
        return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
        }

    int rem = remove(filePath.c_str());
    if(rem != 0) {
        return Status(StatusCode::CANCELLED, "Not able to delete");
    }
    response->set_filename(request->filename());
    response->set_modifiedtime(statResult.st_mtime);

    return Status::OK;
    }




    Status ListFiles(ServerContext* context,
                      const NoArgs* request,
                      ListOfFiles* response) override {
    
        struct dirent *dir_entry;
        DIR *directory = opendir(mount_path.c_str());
   
   if (directory == NULL) {
      return Status::OK;
   }
   while ((dir_entry = readdir(directory)) != NULL) {
       if (context->IsCancelled()) {       
                    closedir(directory);
                    return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
                 }

       if (!dir_entry->d_name || dir_entry->d_name[0] == '.') {
                    continue; 
                }
            std::string filePath = WrapPath(dir_entry->d_name);

         FileDetails *filedetails = response->add_filedetails();
                 struct stat statResult;
                 stat(filePath.c_str(), &statResult);
                 filedetails->set_filename(dir_entry->d_name);
                 filedetails->set_modifiedtime(statResult.st_mtime);
       
   }
   closedir(directory);

        return Status::OK;
    }
 


    Status GetFileStatus(ServerContext* context,
                      const File* request,
                      FileStatus* response) override {

    if (context->IsCancelled()) {       
        return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
    }
        
    struct stat statResult;
    std::string filePath = WrapPath(request->filename());
    //std::string filePath = "/vagrant/pr4/mnt/server/sample-files/gt-einstein2.jpg";
    int res = stat(filePath.c_str(), &statResult);

    if(res == -1) {
        return Status(StatusCode::NOT_FOUND, "File not found");
    }

    response->set_filename(filePath);
    response->set_size(statResult.st_size);
    response->set_creationtime(statResult.st_ctime);
    response->set_modifiedtime(statResult.st_mtime);
    return Status::OK;
    }


};

//
// STUDENT INSTRUCTION:
//
// The following three methods are part of the basic DFSServerNode
// structure. You may add additional methods or change these slightly,
// but be aware that the testing environment is expecting these three
// methods as-is.
//
/**
 * The main server node constructor
 *
 * @param server_address
 * @param mount_path
 */
DFSServerNode::DFSServerNode(const std::string &server_address,
        const std::string &mount_path,
        std::function<void()> callback) :
    server_address(server_address), mount_path(mount_path), grader_callback(callback) {}

/**
 * Server shutdown
 */
DFSServerNode::~DFSServerNode() noexcept {
    dfs_log(LL_SYSINFO) << "DFSServerNode shutting down";
    this->server->Shutdown();
}

/** Server start **/
void DFSServerNode::Start() {
    DFSServiceImpl service(this->mount_path);
    ServerBuilder builder;
    builder.AddListeningPort(this->server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    this->server = builder.BuildAndStart();
    dfs_log(LL_SYSINFO) << "DFSServerNode server listening on " << this->server_address;
    this->server->Wait();
}

//
// STUDENT INSTRUCTION:
//
// Add your additional DFSServerNode definitions here
//

