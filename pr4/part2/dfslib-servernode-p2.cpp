#include <map>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <shared_mutex>
#include <grpcpp/grpcpp.h>

#include "proto-src/dfs-service.grpc.pb.h"
#include "src/dfslibx-call-data.h"
#include "src/dfslibx-service-runner.h"
#include "dfslib-shared-p2.h"
#include "dfslib-servernode-p2.h"

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
using dfs_service::FileContents;
using dfs_service::WriteLock;



//
// STUDENT INSTRUCTION:
//
// Change these "using" aliases to the specific
// message types you are using in your `dfs-service.proto` file
// to indicate a file request and a listing of files from the server
//
using FileRequestType = dfs_service::File;
using FileListResponseType = dfs_service::ListOfFiles;

extern dfs_log_level_e DFS_LOG_LEVEL;

//
// STUDENT INSTRUCTION:
//
// As with Part 1, the DFSServiceImpl is the implementation service for the rpc methods
// and message types you defined in your `dfs-service.proto` file.
//
// You may start with your Part 1 implementations of each service method.
//
// Elements to consider for Part 2:
//
// - How will you implement the write lock at the server level?
// - How will you keep track of which client has a write lock for a file?
//      - Note that we've provided a preset client_id in DFSClientNode that generates
//        a client id for you. You can pass that to the server to identify the current client.
// - How will you release the write lock?
// - How will you handle a store request for a client that doesn't have a write lock?
// - When matching files to determine similarity, you should use the `file_checksum` method we've provided.
//      - Both the client and server have a pre-made `crc_table` variable to speed things up.
//      - Use the `file_checksum` method to compare two files, similar to the following:
//
//          std::uint32_t server_crc = dfs_file_checksum(filepath, &this->crc_table);
//
//      - Hint: as the crc checksum is a simple integer, you can pass it around inside your message types.
//
class DFSServiceImpl final :
    public DFSService::WithAsyncMethod_CallbackList<DFSService::Service>,
        public DFSCallDataManager<FileRequestType , FileListResponseType> {

private:

    /** The runner service used to start the service and manage asynchronicity **/
    DFSServiceRunner<FileRequestType, FileListResponseType> runner;

    /** The mount path for the server **/
    std::string mount_path;

    /** Mutex for managing the queue requests **/
    std::mutex queue_mutex;

    /** The vector of queued tags used to manage asynchronous requests **/
    std::vector<QueueRequest<FileRequestType, FileListResponseType>> queued_tags;


    /**
     * Prepend the mount path to the filename.
     *
     * @param filepath
     * @return
     */
    const std::string WrapPath(const std::string &filepath) {
        return this->mount_path + filepath;
    }

    /** CRC Table kept in memory for faster calculations **/
    CRC::Table<std::uint32_t, 32> crc_table;


    //map will hold locked file and client who has the lock
    std::map<std::string, std::string> lockDetails;

    // we also need a lock whie updating this map;
    std::shared_timed_mutex lockMapMutex;

    //a lock for read and write
    std::shared_timed_mutex readWriteLock;

    std::list<std::string> listOfDeletedFiles;
    std::shared_timed_mutex shm;

public:

    DFSServiceImpl(const std::string& mount_path, const std::string& server_address, int num_async_threads):
        mount_path(mount_path), crc_table(CRC::CRC_32()) {

        this->runner.SetService(this);
        this->runner.SetAddress(server_address);
        this->runner.SetNumThreads(num_async_threads);
        this->runner.SetQueuedRequestsCallback([&]{ this->ProcessQueuedRequests(); });

    }

    ~DFSServiceImpl() {
        this->runner.Shutdown();
    }

    void Run() {
        this->runner.Run();
    }

    /**
     * Request callback for asynchronous requests
     *
     * This method is called by the DFSCallData class during
     * an asynchronous request call from the client.
     *
     * Students should not need to adjust this.
     *
     * @param context
     * @param request
     * @param response
     * @param cq
     * @param tag
     */
    void RequestCallback(grpc::ServerContext* context,
                         FileRequestType* request,
                         grpc::ServerAsyncResponseWriter<FileListResponseType>* response,
                         grpc::ServerCompletionQueue* cq,
                         void* tag) {

        std::lock_guard<std::mutex> lock(queue_mutex);
        this->queued_tags.emplace_back(context, request, response, cq, tag);

    }

    /**
     * Process a callback request
     *
     * This method is called by the DFSCallData class when
     * a requested callback can be processed. You should use this method
     * to manage the CallbackList RPC call and respond as needed.
     *
     * See the STUDENT INSTRUCTION for more details.
     *
     * @param context
     * @param request
     * @param response
     */
    void ProcessCallback(ServerContext* context, FileRequestType* request, FileListResponseType* response) {

        //
        // STUDENT INSTRUCTION:
        //
        // You should add your code here to respond to any CallbackList requests from a client.
        // This function is called each time an asynchronous request is made from the client.
        //
        // The client should receive a list of files or modifications that represent the changes this service
        // is aware of. The client will then need to make the appropriate calls based on those changes.
        //

        Status status = this->CallbackList(context, request, response);
        listOfDeletedFiles.clear();
        if(!status.ok()) {
            dfs_log(LL_ERROR) << "ProcessCallback failed " << status.error_message();
            return;
        }
    }

    void releaseLock(std::string file) {
        lockMapMutex.lock_shared();
        lockDetails.erase(file);
        lockMapMutex.unlock_shared();
    }

    Status CallbackList(ServerContext *context,const FileRequestType* request, FileListResponseType* response ) {
        //erverContext* context,const NoArgs* request,ListOfFiles* response
        NoArgs noargrequest;
        return this->ListFiles(context,&noargrequest,response);
    }

    /**
     * Processes the queued requests in the queue thread
     */
    void ProcessQueuedRequests() {
        while(true) {

            //
            // STUDENT INSTRUCTION:
            //
            // You should add any synchronization mechanisms you may need here in
            // addition to the queue management. For example, modified files checks.
            //
            // Note: you will need to leave the basic queue structure as-is, but you
            // may add any additional code you feel is necessary.
            //


            // Guarded section for queue
            {
                dfs_log(LL_DEBUG2) << "Waiting for queue guard";
                std::lock_guard<std::mutex> lock(queue_mutex);


                for(QueueRequest<FileRequestType, FileListResponseType>& queue_request : this->queued_tags) {
                    this->RequestCallbackList(queue_request.context, queue_request.request,
                        queue_request.response, queue_request.cq, queue_request.cq, queue_request.tag);
                    queue_request.finished = true;
                }

                // any finished tags first
                this->queued_tags.erase(std::remove_if(
                    this->queued_tags.begin(),
                    this->queued_tags.end(),
                    [](QueueRequest<FileRequestType, FileListResponseType>& queue_request) { return queue_request.finished; }
                ), this->queued_tags.end());

            }
        }
    }

    //
    // STUDENT INSTRUCTION:
    //
    // Add your additional code here, including
    // the implementations of your rpc protocol methods.
    //

    //A function to check whether the client and server versions of file is same or different
    Status checkSum(std::multimap<grpc::string_ref, grpc::string_ref> map, std::string filepath) {
        std::multimap<grpc::string_ref, grpc::string_ref>::const_iterator metadata_iterator = map.find("checksum");
        if(metadata_iterator == map.end())
        {
            return Status(StatusCode::CANCELLED, "Cannot get checksum data");
        } 
        std::string checksum = std::string(metadata_iterator->second.begin(),metadata_iterator->second.end());
        std::uint32_t client_crc = std::stoul(checksum.c_str());

        std::uint32_t server_crc = dfs_file_checksum(filepath, &this->crc_table);

        if(client_crc == server_crc) {
            return Status(StatusCode::ALREADY_EXISTS, "checksum is same, hence file is same");
        }
        return Status(StatusCode::OK, "checksum function returned OK");
    }

    //Status status = service_stub->GetWriteLock(&context, request, &writeLock);
    Status GetWriteLock(ServerContext* context,
                       const File* request,
                      WriteLock* response) override {

        
       std::multimap<grpc::string_ref, grpc::string_ref> metadata_iterator = context->client_metadata();
       auto cliID = metadata_iterator.find("clientid");
        if(cliID == metadata_iterator.end())
        {
            return Status(StatusCode::CANCELLED, "Cannot get ClientID");
        } 
        std::string clientID = std::string(cliID->second.begin(),cliID->second.end());

        std::cout<< "gettig lock for " << clientID << " for file " << request->name();
        //first check if client already has lock,, If yes, then just OK
        lockMapMutex.lock_shared();
        auto clientHoldLock = lockDetails.find(request->name());
        std::string clientLock = std::string(clientHoldLock->second);

        //first check if an etry for filename is present or not..
         if(clientHoldLock == lockDetails.end())  {
             //grant lock by adding an entry.
            lockDetails[request->name()] = clientID;
            std::cout << "Obtained lock";
            lockMapMutex.unlock_shared();
         }

         //next checking if the clientID already has the lock.
        else if(clientLock.compare(clientID) == 0) {
            lockMapMutex.unlock_shared();
            std::cout << "client already holds lock on this file";
            return Status(StatusCode::OK, "Client already holds lock on this file");
        }

        //mext check if other client has lock
        else if(clientLock.compare(clientID) != 0) {
            lockMapMutex.unlock_shared();
            std::cout << "some other client has lock of this file";
            return Status(StatusCode::RESOURCE_EXHAUSTED, "some other client has lock of this file");
        }
        return Status::OK;

    }


    //rpc StoreFile(stream FileContents) returns (Result);
    Status StoreFile(ServerContext* context,
                      ServerReader<FileContents>* reader,
                      FileDetails* response) override {

    //get filename
    //shm.lock_shared();
        std::cout << "In server Store..";


    std::multimap<grpc::string_ref, grpc::string_ref> metadata = context->client_metadata();

    auto fileName = metadata.find("filename");
    //Iterator pointing to sought-after element, or end() if not found
    if(fileName == metadata.end()) {
        //shm.unlock_shared();
        return Status(StatusCode::CANCELLED, "Filename metadata not found");
    }
    std::string filenameStr = std::string(fileName->second.begin(),fileName->second.end());
    std::cout << "store filename in server " << filenameStr;


    auto clientID = metadata.find("clientid");
    //Iterator pointing to sought-after element, or end() if not found
    if(clientID == metadata.end()) {
        std::cout << "clientID metadata not found";
                //shm.unlock_shared();

        return Status(StatusCode::CANCELLED, "clientid metadata not found");
    }
    std::string clientIDStr = std::string(clientID->second.begin(),clientID->second.end());

    auto modTime = metadata.find("modifiedtime");
    //Iterator pointing to sought-after element, or end() if not found
    if(modTime == metadata.end()) {
        std::cout << "modTime metadata not found";
                //shm.unlock_shared();

        return Status(StatusCode::CANCELLED, "modTime metadata not found");
    }
    std::string modTimeStr = std::string(modTime->second.begin(),modTime->second.end());
    int modTimeInt = atoi(modTimeStr.c_str());


    std::string filePath = WrapPath(filenameStr);
    std::cout << "file name in store server " << filenameStr ;
    std::string clientLock;

    //first check if there is the lock on the file
    lockMapMutex.lock_shared();
    auto clientHoldLock = lockDetails.find(filenameStr);
    if(clientHoldLock == lockDetails.end()) {
        lockMapMutex.unlock_shared();
        std::cout << "This client doesnt have lock";
                //shm.unlock_shared();

        return Status(StatusCode::CANCELLED, "This client doesnt have lock");

    }
    //std::string clientLock = std::string(clientHoldLock->second.begin(),clientHoldLock->second.end());
    
    else if((clientLock = std::string(clientHoldLock->second.begin(),clientHoldLock->second.end())).compare(clientIDStr) != 0) {

        std::cout << "TESTING :::::::: " << "clientLock -> " << clientLock << " clientIDStr ->  " <<clientIDStr;

        lockMapMutex.unlock_shared();
        std::cout << "some other Client alreday has lock";
                //shm.unlock_shared();

        return Status(StatusCode::CANCELLED, "some other Client alreday has lock");
    }
    lockMapMutex.unlock_shared();

    //check if the file is same in client and sever
    std::cout << "Cecking checksum";
    Status checkChecksum = this->checkSum(metadata, filePath);
    if(!checkChecksum.ok()) {
        if(checkChecksum.error_code() == StatusCode::ALREADY_EXISTS) {
            //check if the modified times are same
            struct stat statres;
            if(stat(filePath.c_str(), &statres) != 0) {
                        //shm.unlock_shared();

                return Status(StatusCode::CANCELLED, "Error ");
            }
            if(statres.st_mtime < modTimeInt) {
                //client has recent file, update modTime
                struct utimbuf new_time;
                new_time.modtime = modTimeInt;
                utime(filePath.c_str(), &new_time);
            }
        }
        releaseLock(filenameStr);
        //shm.unlock_shared();
        std::cout << "checkChecksum.error_message() -> " << checkChecksum.error_message();
        return checkChecksum;
    }

    //write to file
    std::ofstream outfile;
    //outfile.open(filePath, std::ios::trunc);

    FileContents chunck;

    std::cout << "start reading chunks.....";

    while(reader->Read(&chunck)) {
        if (context->IsCancelled()) {   
            releaseLock(filenameStr);
                   // shm.unlock_shared();

            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded"); 
        }   
        if(!outfile.is_open()) {
            outfile.open(filePath, std::ios::trunc);
        }
        const std::string& chunckdata = chunck.chunckcontent();
        outfile << chunckdata;
    }
    outfile.close();
    releaseLock(filenameStr);
    
    //set the response values
    struct stat statResult;
    if(stat(filePath.c_str(), &statResult) != 0) {
              //  shm.unlock_shared();

        return Status(StatusCode::NOT_FOUND, "File not found");
    }
    response->set_filename(filenameStr);
    response->set_modifiedtime(statResult.st_mtime);
       //     shm.unlock_shared();

    return Status::OK;
    }


    //rpc FetchFile(File) returns (stream FileContents) ;
    Status FetchFile(ServerContext* context,
                     const File* request,
                      ServerWriter<FileContents>* writer) override {

    std::cout << "In server Fetch..";
    //std::string filePath = "/vagrant/pr4/mnt/server/sample-files/gt-einstein2.jpg";
    std::string filePath = WrapPath(request->name());

    //first have to check if file is present or not.
    struct stat statResult;
    if(stat(filePath.c_str(), &statResult) != 0) {
        return Status(StatusCode::NOT_FOUND, "File not found");
    }


    Status checkChecksum = this->checkSum(context->client_metadata(), filePath);
    if(!checkChecksum.ok()) {
        if(checkChecksum.error_code() == StatusCode::ALREADY_EXISTS) {
            //check if client and server versions of the file is same..
            std::multimap<grpc::string_ref, grpc::string_ref> metadata = context->client_metadata();
    auto modtime = metadata.find("modifiedtime");
    //Iterator pointing to sought-after element, or end() if not found
    if(modtime == metadata.end()) {
        return Status(StatusCode::CANCELLED, "modtime metadata not found");
    }
    std::string modTimeStr = std::string(modtime->second.begin(),modtime->second.end());
    int modTimeInt = atoi(modTimeStr.c_str());
            //if client has more recent version fetch...
            if(modTimeInt > statResult.st_mtime) {
               struct utimbuf new_time;
               new_time.modtime = modTimeInt;
               new_time.actime = modTimeInt;
               utime(filePath.c_str(), &new_time);
            }
        }
        return checkChecksum;
    }
   
    //if file is present, get the size
    int fileSize = statResult.st_size;
    std::cout << "fileSize in fetch server = " <<fileSize;
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

    std::string filePath = WrapPath(request->name());

    std::multimap<grpc::string_ref, grpc::string_ref> metadata = context->client_metadata();
    auto clientID = metadata.find("clientid");
    //Iterator pointing to sought-after element, or end() if not found
    if(clientID == metadata.end()) {
        std::cout << "delete ---- clientID metadata not found";
        return Status(StatusCode::CANCELLED, "clientID metadata not found");
    }
    std::string clientIDStr = std::string(clientID->second.begin(),clientID->second.end());

    //see if this client has lock of this file.
   /* lockMapMutex.lock_shared();
    auto clientHoldLock = lockDetails.find(request->name());
    std::string clientLock = std::string(clientHoldLock->second.begin(),clientHoldLock->second.end());
    if(clientLock.compare(clientIDStr) == 0) {
        lockMapMutex.unlock_shared();
        std::cout << "delete ---- Client alreday has lock";
        return Status(StatusCode::OK, "Client alreday has lock");
    }
    else if(clientLock.compare(clientIDStr) != 0) {
        lockMapMutex.unlock_shared();
        return Status(StatusCode::CANCELLED, "This client doesnt have lock");
    }
    lockMapMutex.unlock_shared(); */

    std::string clientLock;

    //first check if there is the lock on the file
    lockMapMutex.lock_shared();
    auto clientHoldLock = lockDetails.find(request->name());
    if(clientHoldLock == lockDetails.end()) {
        lockMapMutex.unlock_shared();
        std::cout << "This client doesnt have lock";

        return Status(StatusCode::CANCELLED, "This client doesnt have lock");

    }
    //std::string clientLock = std::string(clientHoldLock->second.begin(),clientHoldLock->second.end());
    
    else if((clientLock = std::string(clientHoldLock->second.begin(),clientHoldLock->second.end())).compare(clientIDStr) != 0) {
        lockMapMutex.unlock_shared();
        std::cout << "some other Client alreday has lock";

        return Status(StatusCode::CANCELLED, "some other Client alreday has lock");
    }
    lockMapMutex.unlock_shared();

    struct stat statResult;
    if(stat(filePath.c_str(), &statResult) != 0) {
        releaseLock(request->name());
        return Status(StatusCode::NOT_FOUND, "File not found");
    }

    if (context->IsCancelled()) {   
        releaseLock(request->name());    
        return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
     }

    int rem = remove(filePath.c_str());
    if(rem != 0) {
        releaseLock(request->name());
        return Status(StatusCode::CANCELLED, "Not able to delete");
    }
    response->set_filename(request->name());
    response->set_modifiedtime(statResult.st_mtime);
    releaseLock(request->name());

    shm.lock_shared();
    listOfDeletedFiles.push_back(request->name());
    shm.unlock_shared();
    return Status::OK;
    }




    Status ListFiles(ServerContext* context,
                      const NoArgs* request,
                      ListOfFiles* response) override {
    
        //readWriteLock.lock_shared();
        struct dirent *dir_entry;
        
        
        DIR *directory = opendir(mount_path.c_str());
   
   if (directory == NULL) {
       //readWriteLock.unlock_shared();
      return Status::OK;
   }
   while ((dir_entry = readdir(directory)) != NULL) {
       /*if (context->IsCancelled()) { 
           readWriteLock.unlock();      
            closedir(directory);
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
        }*/
        std::string filePath = WrapPath(dir_entry->d_name);

        struct stat filetypestat;
            stat(filePath.c_str(), &filetypestat);
            if (!S_ISREG(filetypestat.st_mode)){
                continue;
            }
            

         FileStatus *filedetails = response->add_filestatus();
         struct stat statResult;
         if(stat(filePath.c_str(), &statResult) != 0) {
            //readWriteLock.unlock_shared();
            return Status(StatusCode::NOT_FOUND, "file not found");
         }
        filedetails->set_filename(dir_entry->d_name);
        filedetails->set_modifiedtime(statResult.st_mtime);
        filedetails->set_creationtime(statResult.st_ctime);
        filedetails->set_size(statResult.st_size);

        for(std::string delFile : listOfDeletedFiles) {
            filedetails->set_filename(delFile);
            filedetails->set_size(0);
        }
    }
   closedir(directory);
   //readWriteLock.unlock();
   return Status::OK;
    }
 


    Status GetFileStatus(ServerContext* context,
                      const File* request,
                      FileStatus* response) override {

    if (context->IsCancelled()) {       
        return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded");
    }
        
    struct stat statResult;
    std::string filePath = WrapPath(request->name());
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
// structure. You may add additional methods or change these slightly
// to add additional startup/shutdown routines inside, but be aware that
// the basic structure should stay the same as the testing environment
// will be expected this structure.
//
/**
 * The main server node constructor
 *
 * @param mount_path
 */
DFSServerNode::DFSServerNode(const std::string &server_address,
        const std::string &mount_path,
        int num_async_threads,
        std::function<void()> callback) :
        server_address(server_address),
        mount_path(mount_path),
        num_async_threads(num_async_threads),
        grader_callback(callback) {}
/**
 * Server shutdown
 */
DFSServerNode::~DFSServerNode() noexcept {
    dfs_log(LL_SYSINFO) << "DFSServerNode shutting down";
}

/**
 * Start the DFSServerNode server
 */
void DFSServerNode::Start() {
    DFSServiceImpl service(this->mount_path, this->server_address, this->num_async_threads);


    dfs_log(LL_SYSINFO) << "DFSServerNode server listening on " << this->server_address;
    service.Run();
}

//
// STUDENT INSTRUCTION:
//
// Add your additional definitions here
//
