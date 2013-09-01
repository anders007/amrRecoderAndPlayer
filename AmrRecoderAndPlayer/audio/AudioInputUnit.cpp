#include "AudioInputUnit.h"
#import <AudioToolbox/AudioToolbox.h>
#include <BytesBuffer.h>
#include <IceUtil/IceUtil.h>
#include <SP.h>
#include <interf_enc.h>
#include <stdio.h>
#include <CAStreamBasicDescription.h>
#include <CAXException.h>
//#include "CAStreamBasicDescription.h"
#define kOutputBus 0 /*bus 0 represents a stream to output hardware*/
#define kInputBus 1  /*bus 1 represents a stream from input hardware*/
     


void CheckError(OSStatus error, const char *operation)
{
    if (error == noErr) return;
    
    char errorString[20];
    // See if it appears to be a 4-char-code
    *(UInt32 *)(errorString + 1) = CFSwapInt32HostToBig(error);
    if (isprint(errorString[1]) && isprint(errorString[2]) &&
        isprint(errorString[3]) && isprint(errorString[4])) {
        errorString[0] = errorString[5] = '\'';
        errorString[6] = '\0';
    } else
        // No, format it as an integer
        sprintf(errorString, "%d", (int)error);
    
    fprintf(stderr, "Error: %s (%s)\n", operation, errorString);
    
    //exit(1);
}



#define AMR_MAGIC_NUMBER "#!AMR\n"

typedef struct AUUserData{
    void *inRefCon;
    AudioUnitRenderActionFlags *ioActionFlags;
    AudioTimeStamp *inTimeStamp;
    UInt32 inBusNumber;
    UInt32 inNumberFrames;
    AudioBufferList * ioData;
    OSStatus err;
    
} *AUUserDataRef;

static int SetupRemoteIO (AudioUnit& inRemoteIOUnit, const AURenderCallbackStruct&, const CAStreamBasicDescription& );
//-----------------------------------------------------------------------------------------------------------------------------------------------------------
class EncodeThread : public IceUtil::Thread
{
public:
    EncodeThread(const char* filepath, BytesBufferPtr buffer);
    virtual ~EncodeThread();
    static size_t callBackFun(void* userData, const ChunkInfoRef,  bool terminated);
    
    void stop();
    void cancel();
    virtual void run();
    
private:
    //    PopBufferChunk  chunk;
    std::string _filepath;
    void * armEncodeState;
    bool  _destroy;
    BytesBufferPtr _buffer;
    BufferChunk    _cbChunk;
    unsigned char  armFrame[32];
    FILE *file;
};

typedef IceUtil::Handle<EncodeThread> EncodeThreadPtr;

size_t EncodeThread::callBackFun(void* userData, const ChunkInfoRef info,  bool terminated)
{
#ifdef DEBUG
    
#endif
    
    EncodeThread *This = (EncodeThread*)userData;
    if (info->_data == 0 && terminated) {
        This->_destroy = true;
        SP::printf("nomore data, quit record\n");
        return 0;

    }
    if (info->_size < 160*2) {
        return info->_size;
    }
    int ret = Encoder_Interface_Encode(This->armEncodeState, MR122, (const short*)info->_data, This->armFrame, 0);
    fwrite(This->armFrame, sizeof(unsigned char), ret, This->file);
    return  info->_size;
}


EncodeThread::EncodeThread(const char* filepath, BytesBufferPtr buffer) :
_filepath(filepath)
, _buffer(buffer)
, file(0)
,_destroy(false)
{
    _cbChunk._callback = EncodeThread::callBackFun;
    _cbChunk._userData = this;
}

void EncodeThread::run()
{
    int dtx = 0;
    armEncodeState = Encoder_Interface_init(dtx);
    file = fopen(_filepath.c_str(), "wb+");
    fwrite(AMR_MAGIC_NUMBER, sizeof(char), 6, file);
    do {
        _buffer->eat(160*2, &_cbChunk);
       
    } while (!_destroy);
    //Encoder_Interface_exit(&armEncodeState);

    fclose(file);    
    SP::printf("save file\n");
    
}

EncodeThread::~EncodeThread()
{
    
}


void EncodeThread::stop()
{
    
}

void EncodeThread::cancel()
{
    
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------


//#define RECODESTREAM
class AudioInputUnit_context
{
public:
	static void AudioSessionInterruptionListener(void *inClientData, UInt32 inInterruptionState);
    static void rioInterruptionListener(void *inClientData, UInt32 inInterruption);
    static void propListener(	void *                  inClientData,
                                AudioSessionPropertyID	inID,
                                UInt32                  inDataSize,
                                const void *            inData);
    friend class AudioInputUnit;
    AudioInputUnit_context();
    ~AudioInputUnit_context();
    void initAudioInput();
    void initialize(float sampleRate, int channel, int sampleDeep);
    void uninitialize();
    bool isInitialized();
    void start(const char* path);
    void stop();
    bool isRunning();
    static OSStatus recordingCallback(void *inRefCon,
                             AudioUnitRenderActionFlags *ioActionFlags, 
                             const AudioTimeStamp *inTimeStamp,
                             UInt32 inBusNumber, 
                             UInt32 inNumberFrames, 
                             AudioBufferList *ioData);
    
    static size_t feedCallBackFun(void* userData, const ChunkInfoRef,  bool terminated);
private:
    void enableIO();
    void callbackSetup();
    void setupFomart();
    void setupBuffers();
    inline void makeBufferSilent (AudioBufferList * ioData);
private:
    BytesBufferPtr _buffer;
	AudioComponentInstance _audioUnit;
	//AudioBufferList* _inputBuffer;   // this will hold the latest data from the microphone
    int             _isInitialized;
    CAStreamBasicDescription _audioFormat;
    BufferChunk chunk;

    
    std::string filepath;
    
    EncodeThreadPtr _encoder;
#ifdef RECODESTREAM
    FILE* _recodestreamfile;
#endif    
};


AudioInputUnit_context::AudioInputUnit_context()
:_audioUnit(0)
{
    _isInitialized = false;
    setupBuffers();
}


AudioInputUnit_context::~AudioInputUnit_context()
{
    uninitialize();
}


void AudioInputUnit_context::uninitialize()
{
    if(_isInitialized)        
    {
//        CheckError( AudioUnitUninitialize(_audioUnit),
//                   "Couldn't uninit");
        _isInitialized = false;
    }
}

bool AudioInputUnit_context::isInitialized()
{
    return _isInitialized;
}

void AudioInputUnit_context::AudioSessionInterruptionListener(void *inClientData, UInt32 inInterruptionState)
{
    printf ("Interrupted! inInterruptionState=%ld\n", inInterruptionState);
    AudioInputUnit_context *This = (AudioInputUnit_context*)inClientData;
    
    switch (inInterruptionState) {
        case kAudioSessionBeginInterruption:
            //shutdown audio unit
            break;
        case kAudioSessionEndInterruption:
//            CheckError(AudioQueueStart(appDelegate.audioQueue, 0), \
                       "Couldn't restart the audio queue");
            break;
        default:
            break;
    };
}



void AudioInputUnit_context::propListener(	void *                  inClientData,
                  AudioSessionPropertyID	inID,
                  UInt32                  inDataSize,
                  const void *            inData)
{
    
}

void AudioInputUnit_context::rioInterruptionListener(void *inClientData, UInt32 inInterruption)
{
    try {
        printf("Session interrupted! --- %s ---", inInterruption == kAudioSessionBeginInterruption ? "Begin Interruption" : "End Interruption");
        AudioInputUnit_context *This = (AudioInputUnit_context*)inClientData;
        if (inInterruption == kAudioSessionEndInterruption) {
            // make sure we are again the active session
            XThrowIfError(AudioSessionSetActive(true), "couldn't set audio session active");
            XThrowIfError(AudioOutputUnitStart(This->_audioUnit), "couldn't start unit");
        }
        
        if (inInterruption == kAudioSessionBeginInterruption) {
            XThrowIfError(AudioOutputUnitStop(This->_audioUnit), "couldn't stop unit");
        }
    } catch (CAXException e) {
        char buf[256];
        fprintf(stderr, "Error: %s (%s)\n", e.mOperation, e.FormatError(buf));
    }
}

void AudioInputUnit_context::initialize(float sampleRate, int channel, int sampleDeep) {
    if (_isInitialized) 
        return;
    try { 
        
        // Initialize and configure the audio session
        XThrowIfError(AudioSessionInitialize(NULL, NULL, rioInterruptionListener, this), "couldn't initialize audio session for record");
        
        UInt32 audioCategory = kAudioSessionCategory_RecordAudio;
        XThrowIfError(AudioSessionSetProperty(kAudioSessionProperty_AudioCategory, sizeof(audioCategory), &audioCategory), "couldn't set audio category for record");
        XThrowIfError(AudioSessionAddPropertyListener(kAudioSessionProperty_AudioRouteChange, propListener, this), "couldn't set property listener");
        
        Float32 preferredBufferSize = .02;
        XThrowIfError(AudioSessionSetProperty(kAudioSessionProperty_PreferredHardwareIOBufferDuration, sizeof(preferredBufferSize), &preferredBufferSize), "couldn't set i/o buffer duration");
        
        Float64 hwSampleRate;
        UInt32 size = sizeof(hwSampleRate);
        XThrowIfError(AudioSessionGetProperty(kAudioSessionProperty_CurrentHardwareSampleRate, &size, &hwSampleRate), "couldn't get hw sample rate");
        
        XThrowIfError(AudioSessionSetActive(true), "couldn't set audio session active\n");
        
        
        AURenderCallbackStruct callbackStruct;        
        callbackStruct.inputProc = recordingCallback;
        callbackStruct.inputProcRefCon = this;
        _audioFormat = CAStreamBasicDescription(8000, 1, CAStreamBasicDescription::kPCMFormatInt16, false);
        XThrowIfError(SetupRemoteIO(_audioUnit, callbackStruct, _audioFormat), "couldn't setup remote i/o unit");
    } catch(CAXException &e) {
        char buf[256];
		fprintf(stderr, "Error: %s (%s)\n", e.mOperation, e.FormatError(buf));
    }
    catch (...) {
		fprintf(stderr, "An unknown error occurred\n");
	}
    _isInitialized = true;
}




bool AudioInputUnit_context::isRunning()
{	
	UInt32 auhalRunning = 0, size = 0;

	size = sizeof(auhalRunning);
	if(_audioUnit)
	{
		CheckError( AudioUnitGetProperty(_audioUnit,
                                   kAudioOutputUnitProperty_IsRunning,
                                   kAudioUnitScope_Global,
                                   kInputBus, // input element
                                   &auhalRunning,
                                   &size),
                   "Couldn't get running state");
	}

    return auhalRunning;
}





size_t AudioInputUnit_context::feedCallBackFun(void* userData, const ChunkInfoRef info,  bool terminated)
{
    
    AUUserData& _auUserData = (AUUserData&)*userData;
    AudioInputUnit_context *This = (AudioInputUnit_context*)_auUserData.inRefCon;   

    // Allocate an AudioBufferList plus enough space for
    // array of AudioBuffers
    UInt32 propsize = offsetof(AudioBufferList, mBuffers[0]) + (sizeof(AudioBuffer) *
                                                                This->_audioFormat.mChannelsPerFrame);
    _auUserData.ioData = (AudioBufferList *)malloc(propsize);
    _auUserData.ioData->mNumberBuffers =  This->_audioFormat.mChannelsPerFrame;     //noninterleved

    for (size_t i = 0; i < _auUserData.ioData->mNumberBuffers; ++i) {   //channels
        _auUserData.ioData->mBuffers[0].mNumberChannels = 1;
        _auUserData.ioData->mBuffers[0].mDataByteSize = info->_size/_auUserData.ioData->mNumberBuffers;
        _auUserData.ioData->mBuffers[0].mData = info->_data + i*info->_size/_auUserData.ioData->mNumberBuffers;
    }

    //Get the new audio data

	_auUserData.err = AudioUnitRender(This->_audioUnit,
                          _auUserData.ioActionFlags,
                          _auUserData.inTimeStamp,
                          _auUserData.inBusNumber,
                          _auUserData.inNumberFrames, /* of frames requested*/
                          _auUserData.ioData );/* Audio Buffer List to hold data*/

	
    free(_auUserData.ioData);
    return info->_size;
}

OSStatus AudioInputUnit_context::recordingCallback(void *inRefCon,
                                  AudioUnitRenderActionFlags *ioActionFlags,
                                  const AudioTimeStamp *inTimeStamp,
                                  UInt32 inBusNumber,
                                  UInt32 inNumberFrames,
                                  AudioBufferList * ioData)
{
	AudioInputUnit_context *This = (AudioInputUnit_context *)inRefCon;

    static AUUserData _auUserData = {0};
    _auUserData.inRefCon = This;
    _auUserData.ioActionFlags = ioActionFlags;
    _auUserData.inTimeStamp = (AudioTimeStamp*)inTimeStamp;
    _auUserData.inBusNumber = inBusNumber;
    _auUserData.inNumberFrames = inNumberFrames;
    _auUserData.ioData = ioData;
    _auUserData.err = noErr;
    
    This->chunk._callback = AudioInputUnit_context::feedCallBackFun;
    This->chunk._userData =&_auUserData;
    This->_buffer->feed(inNumberFrames * This->_audioFormat.mBytesPerPacket, &This->chunk);
    if (_auUserData.err) { SP::printf("render: error %d\n", (int)_auUserData.err);}
	return _auUserData.err;
}



void AudioInputUnit_context::setupBuffers()
{
    _buffer = new BytesBuffer(2<<10);
}



void AudioInputUnit_context::start(const char* path)
{
    
	CheckError(AudioOutputUnitStart(_audioUnit),
               "couldn't start unit");
    
#ifdef RECODESTREAM
    //cout << "open file IosServerStream.pcm\n";
    _recodestreamfile = fopen("IosServerStream.pcm", "wb");
#endif
    _encoder = new EncodeThread(path, _buffer);
    _encoder->start();
}

/**
 Stop the audioUnit
 */
void AudioInputUnit_context::stop()
{
    try {
        XThrowIfError(AudioSessionRemovePropertyListenerWithUserData(kAudioSessionProperty_AudioRouteChange, propListener, this), "could not remove PropertyListener");
        
        XThrowIfError(AudioOutputUnitStop(_audioUnit), "couldn't stop audio unit");
        XThrowIfError(AudioUnitUninitialize(_audioUnit), "couldn't uninit audio unit");
        
//        XThrowIfError(AudioSessionRemovePropertyListenerWithUserData(kAudioSessionProperty_AudioRouteChange, propListener, this), "could not remove PropertyListener");
        XThrowIfError(AudioSessionSetActive(false), "couldn't set audio session deactive\n");
    } catch(CAXException &e) {
        char buf[256];
        SP::printf("Error: %s (%s)\n", e.mOperation, e.FormatError(buf));
    }
    catch (...) {
		SP::printf("An unknown error occurred\n");
	}

    _buffer->terminatedFeed();
    _encoder->getThreadControl().join();

}


//----------------------------------------------------------------------------------------------------------------------------------------------------------------




AudioInputUnit& AudioInputUnit::instance()
{
    static AudioInputUnit ref;
    return ref;
}

AudioInputUnit::AudioInputUnit()
{
    _ctx = std::auto_ptr< AudioInputUnit_context>(new AudioInputUnit_context);
}

void AudioInputUnit::uninitialize()
{
    _ctx->uninitialize();
}

bool AudioInputUnit::isInitialized()
{
    return _ctx->isInitialized();
}

void AudioInputUnit::initialize(float sampleRate, int channel, int sampleDeep)
{
    _ctx->initialize(sampleRate, channel, sampleDeep);
}

void AudioInputUnit::start(const char* path)
{
    _ctx->start(path);
}


void AudioInputUnit::stop()
{
    _ctx->stop();
}

void AudioInputUnit::cancel()
{
    
}

bool AudioInputUnit::isRunning()
{
    return _ctx->isRunning();
}

void AudioInputUnit::flush()
{
//    _ctx->_ring->flush();
}



AudioInputUnit::~AudioInputUnit()
{
    
}



int SetupRemoteIO (AudioUnit& inRemoteIOUnit, const AURenderCallbackStruct& inRenderProc, const CAStreamBasicDescription& outFormat)
{
	try {
		// Open the output unit
		AudioComponentDescription desc;
		desc.componentType = kAudioUnitType_Output;
		desc.componentSubType = kAudioUnitSubType_RemoteIO;
		desc.componentManufacturer = kAudioUnitManufacturer_Apple;
		desc.componentFlags = 0;
		desc.componentFlagsMask = 0;
		
		AudioComponent comp = AudioComponentFindNext(NULL, &desc);
		
		XThrowIfError(AudioComponentInstanceNew(comp, &inRemoteIOUnit), "couldn't open the remote I/O unit");
        
		UInt32 one = 1;
		XThrowIfError(AudioUnitSetProperty(inRemoteIOUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, kInputBus, &one, sizeof(one)), "couldn't enable record on the remote I/O unit");
        one = 0;
        XThrowIfError(AudioUnitSetProperty(inRemoteIOUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, kOutputBus, &one, sizeof(one)), "couldn't disable play on the remote I/O unit");
        
		//XThrowIfError(AudioUnitSetProperty(inRemoteIOUnit, kAudioUnitProperty_SetRenderCallback , kAudioUnitScope_Input, kInputBus, &inRenderProc, sizeof(inRenderProc)), "couldn't set remote i/o render callback");
        XThrowIfError(AudioUnitSetProperty(inRemoteIOUnit, kAudioOutputUnitProperty_SetInputCallback , kAudioUnitScope_Input, kInputBus, &inRenderProc, sizeof(inRenderProc)), "couldn't set remote i/o render callback");
		XThrowIfError(AudioUnitSetProperty(inRemoteIOUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, kInputBus, &outFormat, sizeof(outFormat)), "couldn't set the remote I/O unit's input client format");
        // Disable buffer allocation for the recorder (optional - do this if we want to pass in our own)
        UInt32 flag = 0;
        XThrowIfError(AudioUnitSetProperty(inRemoteIOUnit, kAudioUnitProperty_ShouldAllocateBuffer, kAudioUnitScope_Output, kInputBus, &flag, sizeof(flag)), "Could not disable buffer allocation for the recorder");
		XThrowIfError(AudioUnitInitialize(inRemoteIOUnit), "couldn't initialize the remote I/O unit");
	}
	catch (CAXException &e) {
		char buf[256];
        SP::printf( "Error: %s (%s)\n", e.mOperation, e.FormatError(buf));
        
		return 1;
	}
	catch (...) {
		SP::printf( "An unknown error occurred\n");
		return 1;
	}
	
	return 0;
}
