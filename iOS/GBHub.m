#import "GBHub.h"
#pragma clang diagnostic ignored "-Warc-retain-cycles"

NSString *const GBHubStatusChangedNotificationName = @"GBHubStatusChangedNotification";

static NSURL *StringToWebURL(NSString *string)
{
    if (![string isKindOfClass:[NSString class]]) return nil;
    NSURL *url = [NSURL URLWithString:string];
    if (![url.scheme isEqual:@"http"] && [url.scheme isEqual:@"https"]) {
        return nil;
    }
    
    return url;
}

@implementation GBHubGame

- (instancetype)initWithJSON:(NSDictionary *)json
{
    self = [super init];
    
    // Skip NSFW titles
    if ([json[@"nsfw"] boolValue]) return nil;
    
    if (json[@"tags"] && ![json[@"tags"] isKindOfClass:[NSArray class]]) return nil;
    _tags = [NSMutableArray array];

    for (__strong NSString *tag in json[@"tags"]) {
        if (![tag isKindOfClass:[NSString class]]) {
            return nil;
        }
        if ([tag isEqual:@"hw:gbprinter"]) {
            continue;
        }
        
        if ([tag hasPrefix:@"event:"]) {
            tag = [tag substringFromIndex:strlen("event:")];
        }
        if ([tag hasPrefix:@"gb-showdown-"]) {
            tag = [NSString stringWithFormat:@"Game Boy Showdown %@", [tag substringFromIndex:strlen("gb-showdown-")]];
        }
        if ([tag hasPrefix:@"gbcompo"]) {
            tag = [NSString stringWithFormat:@"GBCompo%@", [[tag substringFromIndex:strlen("gbcompo")].capitalizedString stringByReplacingOccurrencesOfString:@"-" withString:@" "]];
        }
        if ([tag isEqual:tag.lowercaseString]) {
            tag = [tag stringByReplacingOccurrencesOfString:@"-" withString:@" "].capitalizedString;
        }
        [(NSMutableArray *)_tags addObject:tag];
    }
    
    NSMutableSet *licenses = [NSMutableSet set];
    
    if (json[@"license"]) {
        [licenses addObject:json[@"license"]];
    }
    if (json[@"gameLicense"]) {
        [licenses addObject:json[@"gameLicense"]];
    }
    if (json[@"assetsLicense"]) {
        [licenses addObject:json[@"assetsLicense"]];
    }
    
    
    if (licenses.count == 1) {
        _license = licenses.anyObject;
        if (![_license isKindOfClass:[NSString class]]) {
            return nil;
        }
        if (!_license.length) _license = nil;
    }
    else if (licenses.count > 1) {
        if (json[@"license"]) {
            return nil;
        }
        _license = [NSString stringWithFormat:@"%@ (Assets: %@)", json[@"gameLicense"], json[@"assetsLicense"]];
    }
    
    if (_license && ![_tags containsObject:@"Open Source"]) {
        // License is guaranteed to be Open Source by spec
        [(NSMutableArray *)_tags addObject:@"Open Source"];
    }
        
    _title = json[@"title"];
    if (![_title isKindOfClass:[NSString class]]) {
        return nil;
    }
    
    _entryDescription = json[@"description"];
    if (_entryDescription && ![_entryDescription isKindOfClass:[NSString class]]) {
        return nil;
    }
    
    _developer = json[@"developer"];
    if (![_developer isKindOfClass:[NSString class]]) {
        if ([_developer isKindOfClass:[NSArray class]] && ((NSArray *)_developer).count) {
            if ([((NSArray *)_developer)[0] isKindOfClass:[NSString class]]) {
                _developer = [(NSArray *)_developer componentsJoinedByString:@", "];
            }
            else if ([((NSArray *)_developer)[0] isKindOfClass:[NSDictionary class]]) {
                NSMutableArray *developers = [NSMutableArray array];
                for (NSDictionary *developer in (NSArray *)_developer) {
                    if (![developer isKindOfClass:[NSDictionary class]]) return nil;
                    NSString *name = developer[@"name"];
                    if (!name) return nil;
                    [developers addObject:name];
                }
                _developer = [developers componentsJoinedByString:@", "];
            }
            else {
                return nil;
            }
        }
        else if ([_developer isKindOfClass:[NSDictionary class]]) {
            _developer = ((NSDictionary *)_developer)[@"name"];
        }
        else {
            return nil;
        }
    }

    
    _slug = json[@"slug"];
    if (![_slug isKindOfClass:[NSString class]]) {
        return nil;
    }

    _type = json[@"typetag"];
    if (![_type isKindOfClass:[NSString class]]) {
        return nil;
    }
    
    NSString *dateString = json[@"date"];
    if ([dateString isKindOfClass:[NSString class]]) {
        NSDateFormatter *dateFormatter = [[NSDateFormatter alloc] init];
        [dateFormatter setDateFormat:@"yyyy-MM-dd"];
        _publicationDate = [dateFormatter dateFromString:dateString];
    }
    
    _repository = StringToWebURL(json[@"repository"]);
    _website = StringToWebURL(json[@"website"]);
    
    NSURL *base = [NSURL URLWithString:[NSString stringWithFormat:@"https://hh3.gbdev.io/static/database-gb/entries/%@", _slug]];
    
    NSMutableArray *screenshots = [NSMutableArray array];
    for (NSString *screenshot in json[@"screenshots"]) {
        [screenshots addObject:[base URLByAppendingPathComponent:screenshot]];
    }
    
    
    _screenshots = screenshots;
    
    for (NSDictionary *file in json[@"files"]) {
        NSString *extension = [file[@"filename"] pathExtension].lowercaseString;
        if (![extension isEqual:@"gb"] && ![extension isEqual:@"gbc"]) {
            // Not a DMG/CGB game
            continue;
        }
        if ([file[@"default"] boolValue] || !_file) {
            _file = [base URLByAppendingPathComponent:file[@"filename"]];
        }
    }
    
    if (!_file) {
        return nil;
    }
    
    _keywords = [NSString stringWithFormat:@"%@ %@ %@ %@",
                 GBSearchCanonicalString(_title),
                 GBSearchCanonicalString(_developer),
                 GBSearchCanonicalString(_entryDescription) ?: @"",
                 GBSearchCanonicalString([_tags componentsJoinedByString:@" "])
    ];
    
    return self;
}

- (NSString *)description
{
    return [NSString stringWithFormat:@"<%@ %p: %@>", self.class, self, self.title];
}

@end

@implementation GBHub
{
    NSMutableDictionary<NSString *, GBHubGame *> *_allGames;
    NSMutableDictionary<NSString *, NSNumber *> *_tags;
    NSMutableArray<GBHubGame *> *_showcaseGames;
    NSSet<NSString *> *_showcaseExtras;
}

+ (instancetype)sharedHub
{
    static GBHub *hub = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        hub = [[self alloc] init];
    });
    return hub;
}

- (void)setStatus:(GBHubStatus)status
{
    if (_status != status) {
        _status = status;
        if ([NSThread isMainThread]) {
            [[NSNotificationCenter defaultCenter] postNotificationName:GBHubStatusChangedNotificationName
                                                                object:self];
        }
        else {
            dispatch_sync(dispatch_get_main_queue(), ^{
                [[NSNotificationCenter defaultCenter] postNotificationName:GBHubStatusChangedNotificationName
                                                                    object:self];
            });
        }
    }
}

- (void)handleAPIData:(NSData *)data forBaseURL:(NSString *)base completion:(void(^)(GBHubStatus))completion
{
    @try {
        if (!data) {
            completion(GBHubStatusError);
            return;
        }
        NSDictionary *json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        if (!json) {
            completion(GBHubStatusError);
            return;
        }
        
        if (!json[@"page_current"] || !json[@"page_total"]) {
            completion(GBHubStatusError);
            return;
        }
        
        for (NSDictionary *entry in json[@"entries"]) {
            @autoreleasepool {
                GBHubGame *game = [[GBHubGame alloc] initWithJSON:entry];
                if (game && !_allGames[game.slug]) {
                    _allGames[game.slug] = game;
                    bool showcase = [_showcaseExtras containsObject:game.slug];
                    if (!showcase) {
                        for (NSString *tag in game.tags) {
                            _tags[tag] = @(_tags[tag].unsignedIntValue + 1);
                            if ([tag containsString:@"Shortlist"]) {
                                showcase = true;
                                break;
                            }
                        }
                    }
                    if (showcase) {
                        [_showcaseGames addObject:game];
                    }
                }
            }
        }
        
        if ([json[@"page_current"] unsignedIntValue] == [json[@"page_total"] unsignedIntValue]) {
            completion(GBHubStatusReady);
            return;
        }
        
        [[[NSURLSession sharedSession] dataTaskWithURL:[NSURL URLWithString:[NSString stringWithFormat:@"%@&page=%u",
                                                                             base,
                                                                             [json[@"page_current"] unsignedIntValue] + 1]]
                                     completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            [self handleAPIData:data forBaseURL:base completion:completion];
        }] resume];
    }
    @catch (NSException *exception) {
        self.status = GBHubStatusError;
    }
}

- (void)addGamesForURL:(NSString *)url completion:(void(^)(GBHubStatus))completion
{
    [[[NSURLSession sharedSession] dataTaskWithURL:[NSURL URLWithString:url]
                                 completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        if (error) {
            completion(GBHubStatusError);
            return;
        }
        [self handleAPIData:data forBaseURL:url completion:completion];
    }] resume];
}

- (unsigned int)countForTag:(NSString *)tag
{
    return _tags[tag].unsignedIntValue;
}

- (void)refresh
{
    if (_status == GBHubStatusInProgress) {
        return;
    }
    self.status = GBHubStatusInProgress;
    _allGames = [NSMutableDictionary dictionary];
    _tags = [NSMutableDictionary dictionary];
    _showcaseGames = [NSMutableArray array];
    
    [[[NSURLSession sharedSession] dataTaskWithURL:[NSURL URLWithString:@"https://sameboy.github.io/ios-showcase"]
                                 completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        if (data) {
            _showcaseExtras = [NSSet setWithArray:[[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] componentsSeparatedByString:@"\n"]];
        }
        [self addGamesForURL:@"https://hh3.gbdev.io/api/search?tags=Open+Source&results=1000"
                  completion:^(GBHubStatus ret) {
            if (ret != GBHubStatusReady) {
                self.status = ret;
                return;
            }
            [self addGamesForURL:@"https://hh3.gbdev.io/api/search?thirdparty=sameboy&results=1000"
                      completion:^(GBHubStatus ret) {
                if (ret == GBHubStatusReady) {
                    _sortedTags = [_tags.allKeys sortedArrayUsingComparator:^NSComparisonResult(NSString *obj1, NSString *obj2) {
                        return [obj1 compare:obj2];
                    }];
                }
                unsigned day = time(NULL) / 60 / 60 / 24;
                if (_showcaseGames.count > 5) {
                    typeof(_showcaseGames) temp = [NSMutableArray array];
                    for (unsigned i = 5; i--;) {
                        unsigned index = day % _showcaseGames.count;
                        GBHubGame *game = _showcaseGames[index];
                        [_showcaseGames removeObjectAtIndex:index];
                        [temp addObject:game];
                    }
                    _showcaseGames = temp;
                }
                self.status = ret;
            }];
        }];
    }] resume];
}

@end
