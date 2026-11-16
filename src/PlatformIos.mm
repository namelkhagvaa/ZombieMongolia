// iOS-ийн жижиг ялгаанууд (Platform.h): хадгалах хавтас, HTTP, холбоос нээх, төхөөрөмжийн дугаар.
// Objective-C++ (Foundation, UIKit); тоглоомын бусад код C++ хэвээр.
#include "Platform.h"

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <dispatch/dispatch.h>

std::string IosDataPath()
{
    // Апп-ын Documents хавтас: апп устгах хүртэл хадгалагдана, iCloud-ийн нөөцөд орно (хадгалалт, данс)
    @autoreleasepool
    {
        NSArray<NSString*>* paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        NSString* folder = paths.firstObject ?: NSTemporaryDirectory();
        return std::string(folder.UTF8String);
    }
}

bool IosHttp(const std::string& method, const std::string& url, const std::string& body, const std::string& token,
             int& status, std::string& response)
{
    status = 0;
    response.clear();
    @autoreleasepool
    {
        NSURL* address = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (!address) return false;
        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:address
                                                               cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                           timeoutInterval:20.0];
        request.HTTPMethod = [NSString stringWithUTF8String:method.c_str()];
        [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
        if (!token.empty())
        {
            [request setValue:[NSString stringWithFormat:@"Bearer %s", token.c_str()] forHTTPHeaderField:@"Authorization"];
        }
        if (!body.empty()) request.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];

        // Хүсэлт бүр өөрийн урсгал дээр (Online::Send): хариу иртэл хүлээнэ
        dispatch_semaphore_t finished = dispatch_semaphore_create(0);
        __block int code = 0;
        __block std::string received;
        NSURLSessionDataTask* task = [NSURLSession.sharedSession
            dataTaskWithRequest:request
              completionHandler:^(NSData* data, NSURLResponse* reply, NSError* error) {
                  if (!error && [reply isKindOfClass:[NSHTTPURLResponse class]])
                  {
                      code = (int)((NSHTTPURLResponse*)reply).statusCode;
                      if (data) received.assign((const char*)data.bytes, data.length);
                  }
                  dispatch_semaphore_signal(finished);
              }];
        [task resume];
        dispatch_semaphore_wait(finished, DISPATCH_TIME_FOREVER);
        status = code;
        response = std::move(received);
        return code != 0;
    }
}

void IosOpenUrl(const std::string& url)
{
    NSString* text = [NSString stringWithUTF8String:url.c_str()];
    dispatch_async(dispatch_get_main_queue(), ^{
        NSURL* address = [NSURL URLWithString:text];
        if (address) [UIApplication.sharedApplication openURL:address options:@{} completionHandler:nil];
    });
}

std::string IosVendorId()
{
    @autoreleasepool
    {
        NSUUID* vendor = UIDevice.currentDevice.identifierForVendor;
        return vendor ? std::string(vendor.UUIDString.UTF8String) : std::string();
    }
}
