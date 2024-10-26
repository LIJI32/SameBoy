#ifdef APPSTORE
#import "GBCommunicator.h"
#import <WatchConnectivity/WatchConnectivity.h>

struct MessageHeader {
    uint32_t index;
    uint32_t count;
};
static const size_t ChunkSize = 0x10000 - sizeof(struct MessageHeader);


@implementation GBCommunicator
{
    NSMutableData *_incomingMessage;
    size_t _incomingChunks;
    size_t _incomingIndex;
    size_t _incomingPos;
    
    NSData *_outgoingReply;
    size_t _outgoingChunks;
    size_t _outgoingIndex;
}

#define _errorcode(x, y) #x "-" #y
#define __errorcode(x, y) _errorcode(x, y)
#define errorcode() __errorcode(__LINE__, TARGET_OS_WATCH)

- (void)handleReceiveReplyWithInitialData:(NSData *)firstChunk
                             replyHandler:(void (^)(NSDictionary<NSString *, id> *replyMessage))replyHandler
                             errorHandler:(void (^)(NSString *error))errorHandler
{
    if (firstChunk.length < sizeof(struct MessageHeader)) {
        errorHandler(@"Communication Error " errorcode());
        return;
    }
    const struct MessageHeader *header = firstChunk.bytes;
    if (header->index != 0) {
        errorHandler((@"Communication Error " errorcode()));
        return;
    }
    if (header->count == 1) {
        NSData *compressed = [firstChunk subdataWithRange:NSMakeRange(sizeof(*header), firstChunk.length - sizeof(*header))];
        replyHandler([NSPropertyListSerialization propertyListWithData:[compressed decompressedDataUsingAlgorithm:NSDataCompressionAlgorithmLZFSE error:nil]
                                                               options:0
                                                                format:nil
                                                                 error:nil]);
        return;
    }
    
    if (firstChunk.length > ChunkSize + sizeof(*header)) abort();
    
    NSMutableData *reply = [NSMutableData dataWithLength:header->count * ChunkSize];
    memcpy(reply.mutableBytes, header + 1, firstChunk.length - sizeof(*header));
    __block size_t pos = firstChunk.length - sizeof(*header);
    __block unsigned currentChunk = 1;
    unsigned chunks = header->count;
    
    __block void (^getChunk)(void) = ^(void) {
        [[WCSession defaultSession] sendMessageData:[NSData data]
                                       replyHandler:^(NSData *replyMessageData) {
            if (replyMessageData.length < sizeof(struct MessageHeader)) {
                errorHandler((@"Communication Error " errorcode()));
                getChunk = nil;
                return;
            }
            const struct MessageHeader *header = replyMessageData.bytes;
            if (header->index != currentChunk ||
                header->count != chunks) {
                errorHandler(@"Communication was interrupted");
                getChunk = nil;
                return;
            }
            if (replyMessageData.length > ChunkSize + sizeof(*header)) abort();
            memcpy((uint8_t *)reply.mutableBytes + pos, header + 1, replyMessageData.length - sizeof(*header));
            pos += replyMessageData.length - sizeof(*header);
            currentChunk++;
            if (currentChunk != chunks) {
                getChunk();
                return;
            }
            reply.length = pos;
            replyHandler([NSPropertyListSerialization propertyListWithData:[reply decompressedDataUsingAlgorithm:NSDataCompressionAlgorithmLZFSE error:nil]
                                                                   options:0
                                                                    format:nil
                                                                     error:nil]);
            getChunk = nil;
            
        }
                                       errorHandler:^(NSError *error) {
            errorHandler(error.localizedDescription);
            getChunk = nil;
        }];
    };
    getChunk();
}


- (void)sendMessage:(NSDictionary<NSString *, id> *)message
       replyHandler:(void (^)(NSDictionary<NSString *, id> *replyMessage))replyHandler
       errorHandler:(void (^)(NSString *error))errorHandler
{
    NSData *data = [NSPropertyListSerialization dataWithPropertyList:message
                                                              format:NSPropertyListBinaryFormat_v1_0
                                                             options:0
                                                               error:nil];
    data = [data compressedDataUsingAlgorithm:NSDataCompressionAlgorithmLZFSE error:nil];
    unsigned chunks = (data.length + ChunkSize - 1) / ChunkSize;
    __block unsigned currentChunk = 0;
    assert(chunks);
    
    __block void (^sendChunk)(void) = ^(void) {
        size_t size = ChunkSize + sizeof(struct MessageHeader);
        if (currentChunk == chunks - 1) {
            size = data.length % ChunkSize + sizeof(struct MessageHeader);
        }
        NSMutableData *chunk = [NSMutableData dataWithLength:size];
        struct MessageHeader *header = chunk.mutableBytes;
        header->index = currentChunk;
        header->count = chunks;
        memcpy(header + 1, data.bytes + currentChunk * ChunkSize, size - sizeof(*header));
        currentChunk++;
        [[WCSession defaultSession] sendMessageData:chunk
                                       replyHandler:^(NSData *replyMessageData) {
            if (currentChunk != chunks) {
                if (replyMessageData.length) {
                    // Data was returned before completion, it's an error string
                    errorHandler([[NSString alloc] initWithData:replyMessageData encoding:NSUTF8StringEncoding]);
                    sendChunk = nil;
                    return;
                }
                sendChunk();
                return;
            }
            [self handleReceiveReplyWithInitialData:replyMessageData replyHandler:replyHandler errorHandler:errorHandler];
            sendChunk = nil;
        }
                                       errorHandler:^(NSError *error) {
            errorHandler(error.localizedDescription);
            sendChunk = nil;
        }];
    };
    sendChunk();
}

- (void)session:(WCSession *)session didReceiveMessageData:(NSData *)messageData replyHandler:(void (^)(NSData *))replyHandler
{
    if (messageData.length) { // Sending a request
        const struct MessageHeader *header = messageData.bytes;
        if (messageData.length < sizeof(*header)) {
            replyHandler([(@"Communication Error " errorcode()) dataUsingEncoding:NSUTF8StringEncoding]);
            return;
        }
        if (header->index == 0) {
            _incomingPos = _incomingIndex = 0;
            _incomingChunks = header->count;
            _incomingMessage = [[NSMutableData alloc] initWithLength:_incomingChunks * ChunkSize];
        }
        if (messageData.length > ChunkSize + sizeof(*header)) abort();
        memcpy((uint8_t *)_incomingMessage.mutableBytes + _incomingPos, header + 1, messageData.length - sizeof(*header));
        _incomingPos += messageData.length - sizeof(*header);
        _incomingIndex++;
        
        if (_incomingIndex == _incomingChunks) {
            _incomingMessage.length = _incomingPos;
            NSData *decompressed = [_incomingMessage decompressedDataUsingAlgorithm:NSDataCompressionAlgorithmLZFSE error:nil];
            if (!decompressed) {
                replyHandler([(@"Communication Error " errorcode()) dataUsingEncoding:NSUTF8StringEncoding]);
                return;
            }
            _incomingMessage = nil;
            NSDictionary *message = [NSPropertyListSerialization propertyListWithData:decompressed
                                                                              options:0
                                                                               format:nil
                                                                                error:nil];
            if (!message) {
                replyHandler([(@"Communication Error " errorcode()) dataUsingEncoding:NSUTF8StringEncoding]);
                return;
            }
            [self session:session didReceiveMessage:message replyHandler:^(NSDictionary<NSString *,id> *replyMessage) {
                NSData *data = [NSPropertyListSerialization dataWithPropertyList:replyMessage
                                                                          format:NSPropertyListBinaryFormat_v1_0
                                                                         options:0
                                                                           error:nil];
                _outgoingReply = [data compressedDataUsingAlgorithm:NSDataCompressionAlgorithmLZFSE error:nil];
                _outgoingChunks = (_outgoingReply.length + ChunkSize - 1) / ChunkSize;
                _outgoingIndex = 0;
                [self session:session didReceiveMessageData:[NSData data] replyHandler:replyHandler];
                
            }];
        }
        else {
            replyHandler([NSData data]);
        }
        
    }
    else { // Getting a reply
        if (!_outgoingReply) {
            replyHandler([(@"Communication Error " errorcode()) dataUsingEncoding:NSUTF8StringEncoding]);
            return;
        }
        size_t size = ChunkSize + sizeof(struct MessageHeader);
        if (_outgoingIndex == _outgoingChunks - 1) {
            size = _outgoingReply.length % ChunkSize + sizeof(struct MessageHeader);
        }
        NSMutableData *chunk = [NSMutableData dataWithLength:size];
        struct MessageHeader *header = chunk.mutableBytes;
        header->index = _outgoingIndex;
        header->count = _outgoingChunks;
        memcpy(header + 1, _outgoingReply.bytes + _outgoingIndex * ChunkSize, size - sizeof(*header));
        _outgoingIndex++;
        if (_outgoingChunks == _outgoingIndex) {
            _outgoingReply = nil;
        }
        replyHandler(chunk);
    }
}

- (void)session:(WCSession *)session activationDidCompleteWithState:(WCSessionActivationState)activationState error:(NSError *)error
{
    
}

#if !TARGET_OS_WATCH
- (void)sessionDidBecomeInactive:(WCSession *)session
{
    
}


- (void)sessionDidDeactivate:(WCSession *)session{
    
}
#endif
@end
#endif
