// http_mac.mm — NSURLSession backend for HttpClient (macOS only, Objective-C++).
//
// NSURLSession is the OS-native HTTP stack on Apple platforms: it uses
// SecureTransport / the system trust store for TLS and honours the system proxy
// (see REVIEW-ROADMAP §9), so like the WinHTTP backend it needs no bundled TLS
// library or CA bundle. The whole file is wrapped in `#if defined(__APPLE__)` so
// it is a no-op elsewhere; it is compiled only on macOS (the Slint Apple build
// already compiles .mm sources for the GUI target — see roadmap §13).
//
// This backend is not built on the Windows CI box, so it is written to be
// correct on first macOS compile: ARC is NOT assumed (the GUI target does not
// enable -fobjc-arc uniformly), so we keep everything in an @autoreleasepool and
// rely on autoreleased objects — no manual retain/release is needed here.

#include "http_client.hpp"

#if defined(__APPLE__)

#import <Foundation/Foundation.h>

#include <string>
#include <vector>

namespace diffy::review {
namespace {

// Map an NSError from the NSURLErrorDomain to our normalized error kind.
HttpError
map_error(NSError* err) {
    if (!err) {
        return HttpError::Internal;
    }
    if ([err.domain isEqualToString:NSURLErrorDomain]) {
        switch (err.code) {
            case NSURLErrorTimedOut:
                return HttpError::Timeout;
            case NSURLErrorCannotFindHost:
            case NSURLErrorCannotConnectToHost:
            case NSURLErrorNetworkConnectionLost:
            case NSURLErrorNotConnectedToInternet:
            case NSURLErrorDNSLookupFailed:
                return HttpError::Network;
            case NSURLErrorSecureConnectionFailed:
            case NSURLErrorServerCertificateHasBadDate:
            case NSURLErrorServerCertificateUntrusted:
            case NSURLErrorServerCertificateHasUnknownRoot:
            case NSURLErrorServerCertificateNotYetValid:
            case NSURLErrorClientCertificateRejected:
            case NSURLErrorClientCertificateRequired:
                return HttpError::Tls;
            case NSURLErrorBadURL:
            case NSURLErrorUnsupportedURL:
                return HttpError::BadUrl;
            default:
                return HttpError::Network;
        }
    }
    return HttpError::Internal;
}

std::string
to_std(NSString* s) {
    if (!s) {
        return std::string();
    }
    const char* c = [s UTF8String];
    return c ? std::string(c) : std::string();
}

}  // namespace
}  // namespace diffy::review

// Session delegate that keeps the Authorization header across redirects to the
// SAME host. NSURLSession strips Authorization when it auto-follows a redirect (a
// deliberate anti-credential-leak default), which breaks providers whose endpoints
// 302 to another path on the same API host — notably Bitbucket's PR diffstat/diff,
// which then arrives unauthenticated and 404s on a private repo. We re-attach the
// token only when the redirect target host matches the original, so it is never
// sent to a third-party host a redirect points at (e.g. a signed media/CDN URL).
@interface DiffyRedirectAuthKeeper : NSObject <NSURLSessionTaskDelegate>
@end

@implementation DiffyRedirectAuthKeeper
- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
                    newRequest:(NSURLRequest*)request
             completionHandler:(void (^)(NSURLRequest*))completionHandler {
    NSString* auth = task.originalRequest.allHTTPHeaderFields[@"Authorization"];
    NSString* fromHost = task.originalRequest.URL.host;
    NSString* toHost = request.URL.host;
    // Host comparison is case-insensitive: DNS names are, and a redirect may echo the
    // host with different casing — a case-sensitive match would drop auth and 404.
    if (auth && fromHost && toHost && [fromHost caseInsensitiveCompare:toHost] == NSOrderedSame &&
        ![request valueForHTTPHeaderField:@"Authorization"]) {
        NSMutableURLRequest* r = [request mutableCopy];
        [r setValue:auth forHTTPHeaderField:@"Authorization"];
        completionHandler(r);
        return;
    }
    completionHandler(request);
}
@end

namespace diffy::review {
namespace {

// One process-wide session that carries the redirect delegate above. Created once;
// NSURLSession is thread-safe and each request blocks its own worker on a semaphore.
NSURLSession*
shared_session() {
    static NSURLSession* session = [] {
        NSURLSessionConfiguration* cfg = [NSURLSessionConfiguration defaultSessionConfiguration];
        return [NSURLSession sessionWithConfiguration:cfg
                                             delegate:[[DiffyRedirectAuthKeeper alloc] init]
                                        delegateQueue:nil];
    }();
    return session;
}

class NsUrlSessionClient final : public HttpClient {
  public:
    HttpResult
    send(const HttpRequest& req) override {
        @autoreleasepool {
            NSString* url_str = [NSString stringWithUTF8String:req.url.c_str()];
            NSURL* url = url_str ? [NSURL URLWithString:url_str] : nil;
            if (!url) {
                HttpResult r;
                r.error = HttpError::BadUrl;
                r.message = "invalid URL";
                return r;
            }

            NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
            request.HTTPMethod = [NSString stringWithUTF8String:req.method.c_str()];
            if (req.timeout_secs > 0) {
                request.timeoutInterval = (NSTimeInterval)req.timeout_secs;
            }
            for (const auto& [name, value] : req.headers) {
                NSString* n = [NSString stringWithUTF8String:name.c_str()];
                NSString* v = [NSString stringWithUTF8String:value.c_str()];
                if (n && v) {
                    [request setValue:v forHTTPHeaderField:n];
                }
            }
            if (!req.body.empty()) {
                request.HTTPBody = [NSData dataWithBytes:req.body.data() length:req.body.size()];
            }

            // Block this worker thread on a semaphore until the completion
            // handler fires. This is safe precisely because send() is documented
            // to run off the UI thread (never on the main run loop).
            __block HttpResult result;
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);

            NSURLSessionDataTask* task = [shared_session()
                dataTaskWithRequest:request
                  completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                      if (error) {
                          result.error = map_error(error);
                          result.message = to_std(error.localizedDescription);
                          dispatch_semaphore_signal(sem);
                          return;
                      }
                      HttpResponse& resp = result.response;
                      if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
                          NSHTTPURLResponse* http = (NSHTTPURLResponse*)response;
                          resp.status = (long)http.statusCode;
                          for (id key in http.allHeaderFields) {
                              id val = http.allHeaderFields[key];
                              resp.headers.emplace_back(to_std((NSString*)key), to_std((NSString*)val));
                          }
                      }
                      if (data && data.length > 0) {
                          resp.body.assign((const char*)data.bytes, data.length);
                      }
                      result.error = HttpError::None;
                      dispatch_semaphore_signal(sem);
                  }];
            [task resume];
            dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
            return result;
        }
    }
};

}  // namespace

std::unique_ptr<HttpClient>
make_http_client() {
    return std::make_unique<NsUrlSessionClient>();
}

}  // namespace diffy::review

#endif  // __APPLE__
