#include <stdio.h>
#include <math.h>
#include <Python.h>
#include "portaudio.h"
#ifdef __PAWIRE_WIN32
#include "pa_win_wasapi.h"
#endif

#define SAMPLE_RATE            (44100)

struct WireConfig_s {
	int isInputInterleaved;
	int isOutputInterleaved;
	int numInputChannels;
	int numOutputChannels;
	int framesPerCallback;
};

typedef struct WireConfig_s WireConfig_t;

typedef struct pawireStats_s {
	int numInputUnderflows;
	int numInputOverflows;
	int numOutputUnderflows;
	int numOutputOverflows;
	int numPrimingOutputs;
	int numCallbacks;
} pawireStats;

typedef struct pawireExportedContext_s {
	struct WireConfig_s config;
	PaStream *stream;
	pawireStats stats;
} pawireExportedContext;

#define USE_FLOAT_INPUT        (1)
#define USE_FLOAT_OUTPUT       (1)

/* Latencies set to defaults. */

#if USE_FLOAT_INPUT
#define INPUT_FORMAT  paFloat32
typedef float INPUT_SAMPLE;
#else
#define INPUT_FORMAT  paInt16
typedef short INPUT_SAMPLE;
#endif

#if USE_FLOAT_OUTPUT
#define OUTPUT_FORMAT  paFloat32
typedef float OUTPUT_SAMPLE;
#else
#define OUTPUT_FORMAT  paInt16
typedef short OUTPUT_SAMPLE;
#endif

double gInOutScaler = 1.0;
#define CONVERT_IN_TO_OUT(in)  ((OUTPUT_SAMPLE) ((in) * gInOutScaler))

typedef struct {
	PaDeviceIndex idx;
	double freq;
	const char *name;
} PaIODevice;

static void describeDevices() {
	PaDeviceIndex nDevs = Pa_GetDeviceCount();
	int i;
	printf("Number of devices: %d\n", nDevs);
	for (i = 0; i < nDevs; ++i) {
		const PaDeviceInfo *dev = Pa_GetDeviceInfo(i);
		printf("Device %s -> Input channels %d, Output channels %d, sample rate %lf\n", dev->name, dev->maxInputChannels, dev->maxOutputChannels, dev->defaultSampleRate);
#ifdef __PAWIRE_WIN32
		if (Pa_GetHostApiInfo(dev->hostApi)->type == paWASAPI) {
			printf("This device is WASAPI compatible.\n");
		}
#endif
		if (i == Pa_GetDefaultInputDevice()) {
			printf("This is the default input device.\n");
		}
	}
}

static const PaHostApiInfo* getApiInfo(PaHostApiTypeId apiType) {
	PaHostApiIndex nApis = Pa_GetHostApiCount();
	int i;
	const PaHostApiInfo *result;
	for (i = 0; i < nApis; ++i) {
		result = Pa_GetHostApiInfo(i);
		if (result->type == apiType) {
			return result;
		}
	}
}

static PaIODevice getInputDevice() {
	PaIODevice result;
	const PaDeviceInfo *dev;
#ifdef __PAWIRE_WIN32
	result.idx = getApiInfo(paWASAPI)->defaultInputDevice;
#else
	result.idx = getApiInfo(paALSA)->defaultInputDevice;
#endif
	result.freq = Pa_GetDeviceInfo(result.idx)->defaultSampleRate;
	result.name = Pa_GetDeviceInfo(result.idx)->name;
	return result;
}

static PaIODevice getOutputDevice() {
	PaIODevice result;
	const PaDeviceInfo *dev;
#ifdef __PAWIRE_WIN32
	result.idx = getApiInfo(paWASAPI)->defaultOutputDevice;
#else
	result.idx = getApiInfo(paALSA)->defaultOutputDevice;
#endif
	result.freq = Pa_GetDeviceInfo(result.idx)->defaultSampleRate;
	result.name = Pa_GetDeviceInfo(result.name)->name;
	return result;
}

/*
 * This routine will be called by the PortAudio engine when audio is needed.
 * It may be called at interrupt level on some machines so don't do anything
 * that could mess up the system like calling malloc() or free().
 */
static int wireCallback( const void *inputBuffer, void *outputBuffer,
		unsigned long framesPerBuffer,
		const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userData ) {
	INPUT_SAMPLE *in;
	OUTPUT_SAMPLE *out;
	int inStride;
	int outStride;
	int inDone = 0;
	int outDone = 0;
	pawireExportedContext *ctx = (pawireExportedContext *) userData;
	WireConfig_t *config = &ctx->config;
	pawireStats *stats = &ctx->stats;
	unsigned int i;
	int inChannel, outChannel;

	/* This may get called with NULL inputBuffer during initial setup. */
	if( inputBuffer == NULL) return 0;

	/* Count flags */
	if( (statusFlags & paInputUnderflow) != 0 ) stats->numInputUnderflows += 1;
	if( (statusFlags & paInputOverflow) != 0 ) stats->numInputOverflows += 1;
	if( (statusFlags & paOutputUnderflow) != 0 ) stats->numOutputUnderflows += 1;
	if( (statusFlags & paOutputOverflow) != 0 ) stats->numOutputOverflows += 1;
	if( (statusFlags & paPrimingOutput) != 0 ) stats->numPrimingOutputs += 1;
	stats->numCallbacks += 1;

	inChannel=0, outChannel=0;
	while( !(inDone && outDone) )
	{
		if( config->isInputInterleaved )
		{
			in = ((INPUT_SAMPLE*)inputBuffer) + inChannel;
			inStride = config->numInputChannels;
		}
		else
		{
			in = ((INPUT_SAMPLE**)inputBuffer)[inChannel];
			inStride = 1;
		}

		if( config->isOutputInterleaved )
		{
			out = ((OUTPUT_SAMPLE*)outputBuffer) + outChannel;
			outStride = config->numOutputChannels;
		}
		else
		{
			out = ((OUTPUT_SAMPLE**)outputBuffer)[outChannel];
			outStride = 1;
		}

		for( i=0; i<framesPerBuffer; i++ )
		{
			*out = CONVERT_IN_TO_OUT( *in );
			out += outStride;
			in += inStride;
		}

		if(inChannel < (config->numInputChannels - 1)) inChannel++;
		else inDone = 1;
		if(outChannel < (config->numOutputChannels - 1)) outChannel++;
		else outDone = 1;
	}
	return 0;
}

static PyObject *makeException(const char *reason) {
	PyErr_SetString(PyExc_RuntimeError, reason);
	return NULL;
}

static  enumerateAPIs() {
	PaHostApiIndex apiCount = Pa_GetHostApiCount();
	PaHostApiIndex i;
	const PaHostApiInfo *info;

	for (PaHostApiIndex i = 0; i < apiCount; ++i) {
		info = Pa_GetHostApiInfo(i);
		printf("Host API: %s (%d devices)\n", info->name, info->deviceCount);
	}
}

static PyObject *startPlayback(PyObject *self, PyObject *args) {
	enumerateAPIs();
	int c;
	PaError err = paNoError;
	PaStream *stream;
	PaStreamParameters inputParameters, outputParameters;
	PaIODevice inputDevice, outputDevice;
	WireConfig_t *config;
	double sampleRateAttemps[] = {44100, 48000, 96000};
	int i;
	double actualSampleRate;

	pawireExportedContext *ctx = malloc(sizeof(*ctx));

#ifdef __PAWIRE_WIN32
	struct PaWasapiStreamInfo wasapiInfo;
	wasapiInfo.size = sizeof(PaWasapiStreamInfo);
	wasapiInfo.hostApiType = paWASAPI;
	wasapiInfo.version = 1;
	wasapiInfo.flags = (paWinWasapiThreadPriority);
	wasapiInfo.threadPriority = eThreadPriorityProAudio;
#endif
	if (!ctx) {
		return makeException("Memory allocation failure.");
	}

    err = Pa_Initialize();
    if( err != paNoError ) {
		return makeException("Failed to initialize portAudio.");
	}
	printf("Initialized :)\n");

	config = &ctx->config;
	ctx->config.isInputInterleaved = 0;
	ctx->config.isOutputInterleaved = 0;
	ctx->config.numInputChannels = 1;
	ctx->config.numOutputChannels = 1;
	ctx->config.framesPerCallback = 64;
	ctx->stats.numInputUnderflows = 0;
	ctx->stats.numInputOverflows = 0;
	ctx->stats.numOutputUnderflows = 0;
	ctx->stats.numOutputOverflows = 0;
	ctx->stats.numPrimingOutputs = 0;
	ctx->stats.numCallbacks = 0;

	printf("input %sinterleaved!\n", (config->isInputInterleaved ? " " : "NOT ") );
	printf("output %sinterleaved!\n", (config->isOutputInterleaved ? " " : "NOT ") );
	printf("framesPerCallback = %d\n", config->framesPerCallback );

	inputDevice = getInputDevice();
	inputParameters.device = inputDevice.idx;
	if (inputParameters.device == paNoDevice) {
		free(ctx);
		return makeException("Error: No default input device.");
	}
	inputParameters.channelCount = 1;
	inputParameters.sampleFormat = INPUT_FORMAT | paNonInterleaved;
	inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
#ifdef __PAWIRE_WIN32
	inputParameters.hostApiSpecificStreamInfo = (&wasapiInfo);
#else
	inputParameters.hostApiSpecificStreamInfo = NULL;
#endif

	outputDevice = getOutputDevice();
	outputParameters.device = outputDevice.idx;            /* default output device */
	if (outputParameters.device == paNoDevice) {
		free(ctx);
		return makeException("Error: No default output device.");
	}
	outputParameters.channelCount = 1;
	outputParameters.sampleFormat = OUTPUT_FORMAT | paNonInterleaved;
	outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
#ifdef __PAWIRE_WIN32
	outputParameters.hostApiSpecificStreamInfo = (&wasapiInfo);
#else
	outputParameters.hostApiSpecificStreamInfo = NULL;
#endif

	for (i = 2; i >= 0; --i) {
		if (Pa_IsFormatSupported(&inputParameters, &outputParameters, sampleRateAttemps[i]) == paFormatIsSupported) {
			actualSampleRate = sampleRateAttemps[i];
			printf("Sample rate in use: %lf\n", actualSampleRate);
			break;
		} else {
			printf("Sample rate %lf not supported for input device %s, output device %s.\n", sampleRateAttemps[i], inputDevice.name, outputDevice.name);
		}
	}

	err = Pa_OpenStream(
			&stream,
			&inputParameters,
			&outputParameters,
			actualSampleRate,
			config->framesPerCallback, /* frames per buffer */
			paClipOff, /* we won't output out of range samples so don't bother clipping them */
			wireCallback,
			config);
	if (err != paNoError) {
		free(ctx);
		return makeException("Failed to open portaudio stream.");
	}

	err = Pa_StartStream( stream );
	if(err != paNoError) {
		free(ctx);
		return makeException("Failed to start portaudio stream.");
	}
	printf("Started stream %p.\n", ctx);
	ctx->stream = stream;
	return PyLong_FromUnsignedLong((unsigned long)(uintptr_t)ctx);
}

static PyObject *stopPlayback(PyObject *self, PyObject *args) {
	unsigned long l;
	pawireExportedContext *ctx;
	PaStream *stream;
	PaError err;
	pawireStats *stats;
	int argOk = PyArg_ParseTuple(args, "k", &l);
	if (!argOk) {
		return NULL;
	}

	ctx = (pawireExportedContext *)(uintptr_t)l;
	printf("Closing stream %p.\n", ctx);
	stream = ctx->stream;
	stats = &ctx->stats;
	err = Pa_CloseStream(stream);
	free(ctx);

#define __PAWIRE_CHECK_FLAG_COUNT(member) \
	if( stats->member > 0 ) printf("FLAGS SET: " #member " = %d\n", stats->member );
	if(err != paNoError) {
		__PAWIRE_CHECK_FLAG_COUNT( numInputUnderflows );
		__PAWIRE_CHECK_FLAG_COUNT( numInputOverflows );
		__PAWIRE_CHECK_FLAG_COUNT( numOutputUnderflows );
		__PAWIRE_CHECK_FLAG_COUNT( numOutputOverflows );
		__PAWIRE_CHECK_FLAG_COUNT( numPrimingOutputs );
		return makeException("Failed to stop portaudio stream.");
	}
#undef __PAWIRE_CHECK_FLAG_COUNT

	printf("number of callbacks = %d\n", stats->numCallbacks );
	Py_RETURN_NONE;
}

static PyObject *
enumerate(PyObject *self, PyObject *args)
{
	Pa_Initialize();
	describeDevices();
	Py_RETURN_NONE;
}

static PyMethodDef SpamMethods[] = {
	{"enumerate",  enumerate, METH_VARARGS,
		"Enumerate devices"},
	{"start_playback",  startPlayback, METH_VARARGS,
		"Start playing microphone"},
	{"stop_playback",  stopPlayback, METH_VARARGS,
		"Stop playing microphone"},
	{NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef pawireModule = {
	PyModuleDef_HEAD_INIT,
	"_pawire", /* name of module */
	NULL, /* module documentation, may be NULL */
	-1, /* size of per-interpreter state of the module,
				 or -1 if the module keeps state in global variables. */
	SpamMethods
};

PyMODINIT_FUNC
PyInit__pawire() {
	PyObject *m = PyModule_Create(&pawireModule);
	if (m == NULL) {
		return NULL;
	}
	return m;
}
