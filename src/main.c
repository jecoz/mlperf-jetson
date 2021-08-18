// gcc preprocessor directive. Enables getopt from unistd.h
#define _POSIX_C_SOURCE 2

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <tensorflow/c/c_api.h>

#include "api/submitter_implemented.h"
#include "api/internally_implemented.h"

#define kNumCols 10
#define kNumRows 49
#define kNumChannels 1
#define kKwsInputSize kNumCols * kNumRows * kNumChannels
#define kCategoryCount 12

int port;
char *line;
const char *model_dir;
const char *tags = "serve";

char* kCategoryLabels[kCategoryCount] = {
	"down", "go", "left", "no", "off", "on",
	"right", "stop", "up", "yes", "silence", "unknown"
};

void
fatale(char *s) {
	fprintf(stderr, "\nerror: %s\n", s);
	exit(1);
}

void
th_serialport_initialize(void) {
	struct termios tty;
	void cfmakeraw(struct termios *__termios_p);

	fprintf(stderr, "initializing serial port..");

	// Note the port is never closed.
	if((port = open(line, O_RDWR)) < 0) {
		fatale("th_serialport_initialize");
	}
	if(tcgetattr(port, &tty) != 0) {
		fatale("th_serialport_initialize");
	}
	cfmakeraw(&tty);
	cfsetispeed(&tty, B115200);
	cfsetospeed(&tty, B115200);

	if(tcsetattr(port, TCSANOW, &tty) != 0) {
		fatale("th_serialport_initialize");
	}

	fprintf(stderr, ".done\n");
}

void
th_timestamp_initialize(void) {
	/* USER CODE 1 BEGIN */
	// Setting up BOTH perf and energy here
	/* USER CODE 1 END */
	/* This message must NOT be changed. */
	th_printf(EE_MSG_TIMESTAMP_MODE);
	/* Always call the timestamp on initialize so that the open-drain output
	is set to "1" (so that we catch a falling edge) */
	th_timestamp();
}

TF_Status *s;
TF_Session *sess;
TF_Graph* g;
TF_SessionOptions *opts;

TF_Output ndin[1], ndout[1];
TF_Tensor *tin[1], *tout[1];

void
th_final_initialize(void) {
	/* 
	inputs['input_1'] tensor_info:
	      dtype: DT_FLOAT
	      shape: (-1, 49, 10, 1)
	      name: serving_default_input_1:0
	The given SavedModel SignatureDef contains the following output(s):
	  outputs['dense'] tensor_info:
	      dtype: DT_FLOAT
	      shape: (-1, 12)
	      name: StatefulPartitionedCall:0
	Method name is: tensorflow/serving/predict
	*/
	fprintf(stderr, "initializing tensors..");

	g = TF_NewGraph();
	s = TF_NewStatus();
	opts = TF_NewSessionOptions();
	sess = TF_LoadSessionFromSavedModel(opts, NULL, model_dir, &tags, 1, g, NULL, s);
	if(TF_GetCode(s) != TF_OK) {
		fatale(TF_Message(s));
	}

	ndin[0] = (TF_Output){TF_GraphOperationByName(g,  "serving_default_input_1"), 0}; 
	if(ndin[0].oper == NULL)
		fatale("serving_default_input_1 not found within graph");

	ndout[0] = (TF_Output){TF_GraphOperationByName(g, "StatefulPartitionedCall"), 0};
	if(ndout[0].oper == NULL)
		fatale("StatefulPartitionedCall not found within graph");

	fprintf(stderr, ".done\n");
	fprintf(stderr, "** READY\n");
}

void
tf_free() {
	TF_DeleteGraph(g);
	TF_DeleteSessionOptions(opts);

	TF_CloseSession(sess, s);
	if(TF_GetCode(s) != TF_OK) {
		fatale(TF_Message(s));
	}
	TF_DeleteSession(sess, s);
	if(TF_GetCode(s) != TF_OK) {
		fatale(TF_Message(s));
	}

	TF_DeleteTensor(tin[0]);
	TF_DeleteTensor(tout[0]);
}

void
th_post() {
}

// Add to this method to return real inference results.
void
th_results() {
	float *ptr;
	TF_Tensor *t;

	fprintf(stderr, "th_results called!\n");

	t = *tout;
	if(t == NULL)
		fatale("output tensor is empty");

	if(TF_TensorElementCount(t) != kCategoryCount)
		fatale("unexpected tensor element count");

	ptr = (float*)TF_TensorData(t);

	th_printf("m-results-[");
	for (int i = 0; i < kCategoryCount; i++) {
		fprintf(stderr, "res(%d,%s) -> %.8f\n", i, kCategoryLabels[i], *ptr);
		th_printf("%f", *ptr++);
		if (i < (kCategoryCount - 1)) {
			th_printf(",");
		}
	}
	th_printf("]\r\n");
}

void
tf_dealloc(void *data, size_t len, void *arg) {
	// Buffer is not dynamically allocated hence does not need to be
	// freed.
}

int8_t bufin[kKwsInputSize];
float buft[kKwsInputSize];
float bufout[kCategoryCount];

// Implement this method to prepare for inference and preprocess inputs.
void
th_load_tensor() {
	static int nload = 0;
	size_t n, len;
	int64_t dimsin[] = {1, kNumRows, kNumCols, 1};
	int64_t dimsout[] = {1, kCategoryCount};
	TF_Tensor *in, *out;

	fprintf(stderr, "%d: th_load_tendor called!\n", nload++);

	// expected input: 10x49 8b MFCC
	len = kKwsInputSize * sizeof(int8_t);
	n = ee_get_buffer(bufin, len);
	if(n != len)
		fatale("ee_get_buffer: short read");
	for(int i = 0; i < kKwsInputSize; i++) {
		buft[i] = (float)bufin[i];
	}

	len = kKwsInputSize * sizeof(float);
	in = TF_NewTensor(TF_FLOAT, dimsin, 4, buft, len, tf_dealloc, NULL);
	if(in == NULL)
		fatale("input tensor allocation failure");
	tin[0] = (TF_Tensor*){in};

	len = kCategoryCount * sizeof(float);
	out = TF_AllocateTensor(TF_FLOAT, dimsout, 2, len);
	if(out == NULL)
		fatale("output tensor allocation failure");
	tout[0] = (TF_Tensor*){out};
}

// Implement this method with the logic to perform one inference cycle.
void
th_infer() {
	static int ninfer = 0;

	fprintf(stderr, "%d: th_infer called!\n", ninfer++);
	TF_SessionRun(
		sess, NULL,
		ndin, tin, 1,
		ndout, tout, 1,
		NULL, 0,
		NULL,
		s
	);
	if(TF_GetCode(s) != TF_OK) {
		fatale(TF_Message(s));
	}
}

void
th_pre() {
}

void
th_command_ready(char volatile *p_command) {
	p_command = p_command;
	ee_serial_command_parser_callback((char *)p_command);
}

// th_libc implementations.
int
th_strncmp(const char *str1, const char *str2, size_t n) {
	return strncmp(str1, str2, n);
}

char*
th_strncpy(char *dest, const char *src, size_t n) {
	return strncpy(dest, src, n);
}

size_t
th_strnlen(const char *str, size_t maxlen) {
	size_t strnlen (const char *__string, size_t __maxlen);
	return strnlen(str, maxlen);
}

char*
th_strcat(char *dest, const char *src) {
	return strcat(dest, src);
}

char*
th_strtok(char *str1, const char *sep) {
	return strtok(str1, sep);
}

int
th_atoi(const char *str) {
	return atoi(str);
}

void* 
th_memset(void *b, int c, size_t len) {
	return memset(b, c, len);
}

void*
th_memcpy(void *dst, const void *src, size_t n) {
	return memcpy(dst, src, n);
}

void
th_timestamp(void) {
	unsigned long microSeconds = 0ul;
	/* USER CODE 2 BEGIN */
	microSeconds = (unsigned long)((uint64_t)clock() * 1000000 / CLOCKS_PER_SEC);
	/* USER CODE 2 END */
	/* This message must NOT be changed. */
	th_printf(EE_MSG_TIMESTAMP, microSeconds);
}

int
th_vprintf(const char *format, va_list ap) {
	int vdprintf(int __fd, const char *__restrict __fmt, __gnuc_va_list __arg);
	return vdprintf(port, format, ap);
}

void
th_printf(const char *p_fmt, ...) {
	va_list args;
	va_start(args, p_fmt);
	(void)th_vprintf(p_fmt, args); /* ignore return */
	va_end(args);
}

char
th_getchar() {
	static char buf[1];
	if(read(port, buf, sizeof(buf)) < 0) {
		fatale("th_getchar");
	}
	// printf("debug: th_getchar: %c\n", buf[0]);
	return buf[0];
}

int
usage(char *name) {
	fprintf(stderr, "usage: %s -d [model dir] -l [serial device, e.g. /dev/ttyPS0]\n", name);
	exit(2);
}

int
main(int argc, char *argv[]) {
	int opt;

	model_dir = "kws_ref_model";
	line = "/dev/ttyTHS1";
	while((opt = getopt(argc, argv, "d:l:")) != -1) {
		switch(opt) {
		case 'd':
			model_dir = optarg;
			break;
		case 'l':
			line = optarg;
			break;
		default:
			printf("opt=%c optarg=%s\n", opt, optarg);
			usage(*argv);
		}
	}

	fprintf(stderr, "line=%s model_dir=%s tf=%s\n", line, model_dir, TF_Version());

	ee_benchmark_initialize();
	while (1) {
		int c;
		c = th_getchar();
		ee_serial_callback(c);
	}
	return 0;
}
