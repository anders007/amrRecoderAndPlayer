//
//  AmrFileRecoder.h
//  AmrRecoderAndPlayer
//
//  Created by lu gang on 8/24/13.
//  Copyright (c) 2013 topcmm. All rights reserved.
//

#import <Foundation/Foundation.h>

@protocol RecodeDelegate
- (CGFloat) timeEscaped;


@end

@interface AmrFileRecoder : NSObject
+ (id) sharedInstance;
- (void) startRecode;
- (void) startRecodeWithFilePath:(NSString*) filepath;
- (void) stopRecode;
@end