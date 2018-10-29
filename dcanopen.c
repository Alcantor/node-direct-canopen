#include <node_api.h>
#include <uv.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>

napi_value g_napi_null;

//// Userful error management //////////////////////////////////////////////////

void napi_throw_last_error(napi_env env){
	napi_status status;
	const napi_extended_error_info* r;
	const char *msg;
	status = napi_get_last_error_info(env, &r);
	if (status == napi_ok) msg = r->error_message;
	else msg = "Unknow error";
	napi_throw_error(env, NULL, msg);
}

void napi_fatal_last_error(napi_env env, const char *file, unsigned int line){
	napi_status status;
	const napi_extended_error_info* r;
	const char *msg;
	char location[64];
	sprintf(location, "%s:%u", file, line);
	status = napi_get_last_error_info(env, &r);
	if (status == napi_ok) msg = r->error_message;
	else msg = "Unknow fatal error";
	napi_fatal_error(location, NAPI_AUTO_LENGTH, msg, NAPI_AUTO_LENGTH);
}

#define napi_assert(env, status) { \
	if (status != napi_ok) { \
		napi_throw_last_error(env); \
		return g_napi_null; \
	} \
}

#define napi_assert_other(env, condition, message) { \
	if (condition) { \
		napi_throw_error(env, NULL, message); \
		return g_napi_null; \
	} \
}

#define napi_assert_async(env, status, scope) { \
	if (status != napi_ok) { \
		napi_fatal_last_error(env, __FILE__, __LINE__); \
		napi_close_handle_scope(env, scope); \
		return; \
	} \
}

napi_status napi_create_error_utf8(napi_env env, const char *msg, napi_value *result) {
	napi_status status;	
	napi_value nmsg;
	status = napi_create_string_utf8(env, msg, NAPI_AUTO_LENGTH, &nmsg);
	if (status != napi_ok) return status;
	status = napi_create_error(env, NULL, nmsg, result);
	return status;
}

//// CANopen structures ////////////////////////////////////////////////////////

/* NMT State */
enum co_e_nmt_state {
	CO_NMT_OPERATIONAL=0x01,
	CO_NMT_STOP=0x02,
	CO_NMT_PRE_OPERATIONAL=0x80,
	CO_NMT_RESET_NODE=0x81,
	CO_NMT_RESET_COMMUNICATION=0x82
};

/* NMT Object */
struct co_s_nmt {
	enum co_e_nmt_state state : 8;
	uint8_t node_id;
} __attribute__((packed));

/* Client command  specifier (Master to Node) */
enum co_e_sdo_ccs {
	CO_CCS_DOWNLOAD_INIT=1,
	CO_CCS_DOWNLOAD_SEGMENT=0,
	CO_CCS_UPLOAD_INIT=2,
	CO_CCS_UPLOAD_SEGMENT=3,
	CO_CCS_ABORT  =4,
	CO_CCS_NOTHING=-1
};

/* Server command  specifier (Node to Master)  */
enum co_e_sdo_scs {
	CO_SCS_DOWNLOAD_INIT_RESPONSE=3,
	CO_SCS_DOWNLOAD_SEGMENT_RESPONSE=1,
	CO_SCS_UPLOAD_INIT_RESPONSE=2,
	CO_SCS_UPLOAD_SEGMENT_RESPONSE=0,
	CO_SCS_ABORT=4,
	CO_SCS_NOTHING=-1
};

/* SDO Object */
struct co_s_sdo {
	union{
		struct {
			uint8_t s  : 1; /* size is specified in n */
			uint8_t e  : 1; /* expedited */
			uint8_t n  : 2; /* unused bytes length in data */
			uint8_t r  : 1; /* reserve */
			uint8_t cs : 3; /* command specifier */
		} bits;
		uint8_t byte;
	} header;
	uint16_t index;
	uint8_t  subindex;
	uint8_t  data[4];
} __attribute__((packed));

/* PDO IDs*/
enum co_e_pdo_id {
	CO_PDO_ID0=0,
	CO_PDO_ID1=1,
	CO_PDO_ID2=2,
	CO_PDO_ID3=3
};

/* PDO Object */
struct co_s_pdo {
	uint8_t  data[8];
} __attribute__((packed));

//// SDO Queue /////////////////////////////////////////////////////////////////

#define QSIZE 64

typedef struct {
	napi_ref cb_ref;
	napi_async_context cb_ctx;
	struct can_frame cf;
	enum co_e_sdo_scs expected_scs;
} co_t_sdo_queue_item;

typedef struct {
	co_t_sdo_queue_item items[QSIZE];
	unsigned int head, tail;
}co_t_sdo_queue;

void co_sdo_queue_reset(co_t_sdo_queue *q){
	q->head = q->tail = 0;
}

unsigned int co_sdo_queue_size(co_t_sdo_queue *q){
	//if(q->tail > q->head) printf("Queue inconsistency error!\n");
	return q->head - q->tail;
}

co_t_sdo_queue_item *co_sdo_queue_pop(co_t_sdo_queue *q){
	/* Return NULL if queue is empty */
	if(co_sdo_queue_size(q) == 0) return NULL;

	/* POP */
	return &q->items[q->tail++];
}

co_t_sdo_queue_item *co_sdo_queue_get(co_t_sdo_queue *q){
	/* Return NULL if queue is empty */
	if(co_sdo_queue_size(q) == 0) return NULL;

	/* GET (do not remove from stack) */
	return &q->items[q->tail];
}

co_t_sdo_queue_item *co_sdo_queue_push(co_t_sdo_queue *q){
	/* If the queue is empty, reset it directly */
	if(co_sdo_queue_size(q) == 0) co_sdo_queue_reset(q);

	/* Return NULL if queue is full */
	if(q->head >= QSIZE) return NULL;

	/* PUSH */
	return &q->items[q->head++];
}

//// Node structure ////////////////////////////////////////////////////////////
typedef struct {	
	napi_env env;
	canid_t node_id;
	int canfd;
	co_t_sdo_queue sdo_queue;
	uv_poll_t sdo_uvp;
	uv_timer_t sdo_uvt;
	unsigned int wait_time;
	napi_ref cb_pdo_ref;
	napi_async_context cb_pdo_ctx;
} co_t_node;

//// uvlib callback ////////////////////////////////////////////////////////////
void co_sdo_emit(co_t_node *con);
void co_sdo_timeout_cb(uv_timer_t* handle){
	co_t_node *con = (co_t_node *)handle->data;
	co_t_sdo_queue_item *i;
	napi_handle_scope nhs;
	napi_status status;
	napi_value argv[1], global, cb;

	i = co_sdo_queue_pop(&con->sdo_queue);
	if(i == NULL) return;
	napi_open_handle_scope(con->env, &nhs);
	co_sdo_emit(con);

	/* Parameter error details */
	status = napi_create_string_utf8(con->env, "Timeout SDO Response",
		NAPI_AUTO_LENGTH, &argv[0]);
	napi_assert_async(con->env, status, nhs);

	/* Call the callback */
	status = napi_get_global(con->env, &global);
	napi_assert_async(con->env, status, nhs);
	status = napi_get_reference_value(con->env, i->cb_ref, &cb);
	napi_assert_async(con->env, status, nhs);
	status = napi_make_callback(con->env, i->cb_ctx, global, cb, 1, argv, NULL);
	napi_assert_async(con->env, status, nhs);

	napi_close_handle_scope(con->env, nhs);
}

void co_sdo_emit(co_t_node *con){
	struct can_frame *frame;
	/* Return directly, if the queue is empty */
	if(co_sdo_queue_size(&con->sdo_queue) == 0)
		return;
	frame = &co_sdo_queue_get(&con->sdo_queue)->cf;
	if(write(con->canfd, frame, sizeof(struct can_frame)) < 0)
		napi_throw_error(con->env, NULL, "Cannot write socket");

	uv_timer_start(&con->sdo_uvt, co_sdo_timeout_cb, con->wait_time, 0);
}

void co_recv_sdo(co_t_node *con, struct co_s_sdo *s){
	co_t_sdo_queue_item *i;
	napi_handle_scope nhs;
	napi_status status;
	napi_value argv[1], global, cb;
	void *jsdata;
	size_t jslen;

	/* We receive a SDO: stop timer and send the next SDO */
	i = co_sdo_queue_pop(&con->sdo_queue);
	if(i == NULL) return;
	uv_timer_stop(&con->sdo_uvt);	
	napi_open_handle_scope(con->env, &nhs);
	co_sdo_emit(con);

	/* Check the type of SDO */
	if(s->header.bits.cs != i->expected_scs) {
		/* Error details */
		status = napi_create_error_utf8(con->env, "Unexpected SDO response", &argv[0]);
		napi_assert_async(con->env, status, nhs);
	/* We only support SDO upload up to 4 bytes */
	}else if(s->header.bits.cs == CO_SCS_UPLOAD_INIT_RESPONSE &&
			(s->header.bits.e != 1 || s->header.bits.s != 1)) {
		/* Error details */
		status = napi_create_error_utf8(con->env, "Unimplemented SDO response (length >4)", &argv[0]);
		napi_assert_async(con->env, status, nhs);
	}else{
		/* Set the data */
		jslen = 4-s->header.bits.n;
		status = napi_create_arraybuffer(con->env, jslen, &jsdata, &argv[0]);
		napi_assert_async(con->env, status, nhs);
		memcpy(jsdata, s->data, jslen);
	}

	/* Call the callback */
	status = napi_get_global(con->env, &global);
	napi_assert_async(con->env, status, nhs);
	status = napi_get_reference_value(con->env, i->cb_ref, &cb);
	napi_assert_async(con->env, status, nhs);
	status = napi_make_callback(con->env, i->cb_ctx, global, cb, 1, argv, NULL);
	napi_assert_async(con->env, status, nhs);

	napi_close_handle_scope(con->env, nhs);
}

void co_recv_pdo(co_t_node *con, struct can_frame *frame){
	napi_handle_scope nhs;
	napi_status status;
	napi_value argv[2], global, cb;
	void *jsdata;
	size_t jslen;

	/* No callback, do nothing. */
	if(con->cb_pdo_ref == NULL) return;

	napi_open_handle_scope(con->env, &nhs);

	/* 1. Parameter is the PDO id */
	status = napi_create_uint32(con->env, ((frame->can_id - 0x100) >> 8) & 3, &argv[0]);
	napi_assert_async(con->env, status, nhs);

	/* 2. Parameter is the data */
	jslen = frame->can_dlc;
	status = napi_create_arraybuffer(con->env, jslen, &jsdata, &argv[1]);
	napi_assert_async(con->env, status, nhs);
	memcpy(jsdata, frame->data, jslen);

	/* Call the callback */
	status = napi_get_global(con->env, &global);
	napi_assert_async(con->env, status, nhs);
	status = napi_get_reference_value(con->env, con->cb_pdo_ref, &cb);
	napi_assert_async(con->env, status, nhs);
	status = napi_make_callback(con->env, con->cb_pdo_ctx, global, cb, 2, argv, NULL);
	napi_assert_async(con->env, status, nhs);

	napi_close_handle_scope(con->env, nhs);
}

void co_recv_cb(uv_poll_t* handle, int status, int events) {
	co_t_node *con = (co_t_node *)handle->data;
	struct can_frame frame;
	int err;

	err = read(con->canfd, &frame, sizeof(struct can_frame));
	if(err != sizeof(struct can_frame))
		return; /* Ignore invalid can frame */

	/* Receive an SDO */
	if(frame.can_id == 0x580+con->node_id){
		co_recv_sdo(con, (struct co_s_sdo *)frame.data);
	/* PDO 0-3 */
	}else if((frame.can_id == 0x180+con->node_id || frame.can_id == 0x280+con->node_id ||
		frame.can_id == 0x380+con->node_id || frame.can_id == 0x480+con->node_id)){
 		co_recv_pdo(con, &frame);
	}
}

//// NMT Functions /////////////////////////////////////////////////////////////

napi_value wrapper_co_nmt_send(napi_env env, napi_callback_info info){
	napi_status status;
	size_t argc = 1;
	napi_value argv[1];
	co_t_node *con;
	uint32_t state; /* Use uint32_t and not enum co_e_nmt_state */
	struct can_frame frame;
	struct co_s_nmt *d = (struct co_s_nmt *)frame.data;

	/* Get arguments */
	status = napi_get_cb_info(env, info, &argc, argv, NULL, (void **)&con);
	napi_assert(env, status);

	/* 1. Parameter is the state to send */
	status = napi_get_value_uint32(env, argv[0], &state);
	napi_assert(env, status);

	frame.can_id = 0x000;
	d->state = state;
	d->node_id = con->node_id;
	frame.can_dlc = sizeof(struct co_s_nmt);

	if(write(con->canfd, &frame, sizeof(struct can_frame)) < 0)
		napi_throw_error(con->env, NULL, "Cannot write socket");

	return g_napi_null;
}

//// SDO Functions /////////////////////////////////////////////////////////////

napi_value wrapper_co_sdo_download(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 4;
	napi_value argv[4], tmp;
	uint32_t index, subindex;
	void *jsdata;
	size_t jslen;
	napi_valuetype vt;
	co_t_node *con;
	struct can_frame *frame;
	struct co_s_sdo *s;
	co_t_sdo_queue_item *i;
	uint8_t unused_bytes;

	/* Get arguments */
	status = napi_get_cb_info(env, info, &argc, argv, NULL, (void **)&con);
	napi_assert(env, status);

	/* 1. Parameter is the index */
	status = napi_get_value_uint32(env, argv[0], &index);
	napi_assert(env, status);

	/* 2. Parameter is the subindex */
	status = napi_get_value_uint32(env, argv[1], &subindex);
	napi_assert(env, status);

	/* 3. Parameter is the data */
	status = napi_get_arraybuffer_info(env, argv[2], &jsdata, &jslen);
	napi_assert(env, status);
	napi_assert_other(env, jslen > 4, "Unimplemented SDO request (length >4)");

	/* 4. Parameter is the callback */
	status = napi_typeof(env, argv[3], &vt);
	napi_assert(env, status);
	napi_assert_other(env, vt != napi_function, "Invalid callback");

	/* Get a free queue item */
	i = co_sdo_queue_push(&con->sdo_queue);
	napi_assert_other(env, i == NULL, "SDO queue full!")

	/* Save the callback */
	status = napi_create_string_utf8(env, "SDO Callback Context", NAPI_AUTO_LENGTH, &tmp);
	napi_assert(env, status);
	status = napi_async_init(env, NULL, tmp, &i->cb_ctx);
	napi_assert(env, status);
	status = napi_create_reference(env, argv[3], 1, &i->cb_ref);
	napi_assert(env, status);

	/* Fill the CANopen data */
	frame = &i->cf;
	s = (struct co_s_sdo *) frame->data;
	frame->can_id = 0x600+con->node_id;
	unused_bytes = 4 - jslen;
	s->header.bits.cs = CO_CCS_DOWNLOAD_INIT;
	s->header.bits.r = 0;
	s->header.bits.n = unused_bytes;
	s->header.bits.e = 1;
	s->header.bits.s = 1;
	s->index = index;
	s->subindex = subindex;
	memcpy(s->data, jsdata, jslen);
	memset(&s->data[jslen], 0, unused_bytes);
	frame->can_dlc = sizeof(struct co_s_sdo);

	/* Expected command specifier */
	i->expected_scs = CO_SCS_DOWNLOAD_INIT_RESPONSE;

	/* Send if needed */
	if(co_sdo_queue_size(&con->sdo_queue) == 1)
		co_sdo_emit(con);

	return g_napi_null;
}

napi_value wrapper_co_sdo_upload(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 3;
	napi_value argv[3], tmp;
	napi_valuetype vt;
	co_t_node *con;
	uint32_t index, subindex;
	struct can_frame *frame;
	struct co_s_sdo *s;
	co_t_sdo_queue_item *i;

	/* Get arguments */
	status = napi_get_cb_info(env, info, &argc, argv, NULL, (void **)&con);
	napi_assert(env, status);

	/* 1. Parameter is the index */
	status = napi_get_value_uint32(env, argv[0], &index);
	napi_assert(env, status);

	/* 2. Parameter is the subindex */
	status = napi_get_value_uint32(env, argv[1], &subindex);
	napi_assert(env, status);

	/* 3. Parameter is the callback */
	status = napi_typeof(env, argv[2], &vt);
	napi_assert(env, status);
	napi_assert_other(env, vt != napi_function, "Invalid callback");

	/* Get a free queue item */
	i = co_sdo_queue_push(&con->sdo_queue);
	napi_assert_other(env, i == NULL, "SDO queue full!");

	/* Save the callback */
	status = napi_create_string_utf8(env, "SDO Callback Context", NAPI_AUTO_LENGTH, &tmp);
	napi_assert(env, status);
	status = napi_async_init(env, NULL, tmp, &i->cb_ctx);
	napi_assert(env, status);
	status = napi_create_reference(env, argv[2], 1, &i->cb_ref);
	napi_assert(env, status);

	/* Fill the CANopen data */
	frame = &i->cf;
	s = (struct co_s_sdo *) frame->data;
	frame->can_id = 0x600+con->node_id;
	s->header.bits.cs = CO_CCS_UPLOAD_INIT;
	s->header.bits.r = 0;
	s->header.bits.n = 0;
	s->header.bits.e = 0;
	s->header.bits.s = 0;
	s->index = index;
	s->subindex = subindex;
	memset(s->data, 0, sizeof(s->data));
	frame->can_dlc = sizeof(struct co_s_sdo);

	/* Expected command specifier */
	i->expected_scs = CO_SCS_UPLOAD_INIT_RESPONSE;

	/* Send if needed */
	if(co_sdo_queue_size(&con->sdo_queue) == 1)
		co_sdo_emit(con);

	return g_napi_null;
}

//// PDO Functions /////////////////////////////////////////////////////////////

napi_value wrapper_co_pdo_send(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 2;
	napi_value argv[2];
	uint32_t pdoid;
	size_t jslen;
	void *jsdata;
	co_t_node *con;
	struct can_frame frame;

	/* Get arguments */
	status = napi_get_cb_info(env, info, &argc, argv, NULL, (void **)&con);	
	napi_assert(env, status);

	/* 1. Parameter is the PDO Id */
	status = napi_get_value_uint32(env, argv[0], &pdoid);
	napi_assert(env, status);

	/* 2. Parameter is the data */
	status = napi_get_arraybuffer_info(env, argv[1], &jsdata, &jslen);
	napi_assert(env, status);
	napi_assert_other(env, jslen > 8, "PDO length > 8 bytes");

	/* Fill the CANopen data */
	frame.can_id = ((0x100*pdoid)+0x200) | con->node_id;
	memcpy(frame.data, jsdata, jslen);
	frame.can_dlc = jslen;

	if(write(con->canfd, &frame, sizeof(struct can_frame)) < 0)
		napi_throw_error(con->env, NULL, "Cannot write socket");

	return g_napi_null;
}

napi_value wrapper_co_pdo_recv(napi_env env, napi_callback_info info) {
	napi_status status;
	size_t argc = 1;
	napi_value argv[1], tmp;
	napi_valuetype vt;
	co_t_node *con;

	/* Get arguments */
	status = napi_get_cb_info(env, info, &argc, argv, NULL, (void **)&con);
	napi_assert(env, status);

	/* 1. Parameter is the callback */
	status = napi_typeof(env, argv[0], &vt);
	napi_assert(env, status);
	napi_assert_other(env, vt != napi_function, "Invalid callback");
	status = napi_create_string_utf8(env, "PDO Callback Context", NAPI_AUTO_LENGTH, &tmp);
	napi_assert(env, status);
	status = napi_async_init(env, NULL, tmp, &con->cb_pdo_ctx);
	napi_assert(env, status);
	status = napi_create_reference(env, argv[0], 1, &con->cb_pdo_ref);
	napi_assert(env, status);

	return g_napi_null;
}

//// Create Node Function //////////////////////////////////////////////////////

void wrapper_co_delete_node(napi_env env, void* finalize_data, void* finalize_hint){
	co_t_node *con = (co_t_node *)finalize_data;
	uv_poll_stop(&con->sdo_uvp);
	uv_close((uv_handle_t *)&con->sdo_uvp, NULL);
	uv_timer_stop(&con->sdo_uvt);
	uv_close((uv_handle_t *)&con->sdo_uvt, NULL);
	free(con);
}

napi_value wrapper_co_create_node(napi_env env, napi_callback_info info) {
	napi_status status;
	uv_loop_t *loop;

	size_t argc = 2;
	napi_value argv[2];

	co_t_node * con;
	char device[16];
	uint32_t node_id;

	napi_value object, tmp;
	int err;
	
	struct can_filter rfilter[8];
	struct ifreq ifr;
	struct sockaddr_can addr;

	/* Get arguments */
	status = napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
	napi_assert(env, status);

	/* 1. Parameter is the can string device: can0 */
	status = napi_get_value_string_utf8(env, argv[0], device, sizeof(device), NULL);
	napi_assert(env, status);

	/* 2. Paramater is the can id to talk with */
	status = napi_get_value_uint32(env, argv[1], &node_id);
	napi_assert(env, status);

	/* Create a new object */
	status = napi_create_object(env, &object);
	napi_assert(env, status);

	/* ._co_t_node hold owner private data */
	con = (co_t_node *)malloc(sizeof(co_t_node));
	status = napi_create_external(env, con, wrapper_co_delete_node, NULL, &tmp);
	napi_assert(env, status);
	status = napi_set_named_property(env, object, "_co_t_node", tmp);
	napi_assert(env, status);

	/* .nmt_send Function*/
	status = napi_create_function(env, NULL, 0, wrapper_co_nmt_send, (void *)con, &tmp);
	napi_assert(env, status);
	status = napi_set_named_property(env, object, "nmt_send", tmp);
	napi_assert(env, status);

	/* .sdo_download Function*/
	status = napi_create_function(env, NULL, 0, wrapper_co_sdo_download, (void *)con, &tmp);
	napi_assert(env, status);
	status = napi_set_named_property(env, object, "sdo_download", tmp);
	napi_assert(env, status);

	/* .sdo_upload Function*/
	status = napi_create_function(env, NULL, 0, wrapper_co_sdo_upload, (void *)con, &tmp);
	napi_assert(env, status);
	status = napi_set_named_property(env, object, "sdo_upload", tmp);
	napi_assert(env, status);

	/* .pdo_send Function*/
	status = napi_create_function(env, NULL, 0, wrapper_co_pdo_send, (void *)con, &tmp);
	napi_assert(env, status);
	status = napi_set_named_property(env, object, "pdo_send", tmp);
	napi_assert(env, status);

	/* .pdo_recv Function*/
	status = napi_create_function(env, NULL, 0, wrapper_co_pdo_recv, (void *)con, &tmp);
	napi_assert(env, status);
	status = napi_set_named_property(env, object, "pdo_recv", tmp);
	napi_assert(env, status);

	/* .node_id Value */
	status = napi_create_uint32(env, node_id, &tmp);
	napi_assert(env, status);
	status = napi_set_named_property(env, object, "node_id", tmp);
	napi_assert(env, status);

	/* Set node id */
	con->env = env;
	con->node_id = (uint8_t) node_id;

	/* Prepare filter */
	/* Emergency */
	rfilter[0].can_id   = 0x080+node_id;
	rfilter[0].can_mask = 0x7FF; //0x780;
	/* PDO 0 */
	rfilter[1].can_id   = 0x180+node_id;
	rfilter[1].can_mask = 0x7FF; //0x780;
	/* PDO 1 */
	rfilter[2].can_id   = 0x280+node_id;
	rfilter[2].can_mask = 0x7FF; //0x780;
	/* PDO 2 */
	rfilter[3].can_id   = 0x380+node_id;
	rfilter[3].can_mask = 0x7FF; //0x780;
	/* PDO 3 */
	rfilter[4].can_id   = 0x480+node_id;
	rfilter[4].can_mask = 0x7FF; //0x780;
	/* SDO */
	rfilter[5].can_id   = 0x580+node_id;
	rfilter[5].can_mask = 0x7FF; //0x780;
	/* NMT */
	rfilter[6].can_id   = 0x700+node_id;;
	rfilter[6].can_mask = 0x7FF; //0x780;
	/* LSS */
	rfilter[7].can_id   = 0x7E4;
	rfilter[7].can_mask = 0x7FF;

	/* Create Socket */
	con->canfd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	napi_assert_other(env, con->canfd < 0, "Cannot create socket");
	setsockopt(con->canfd, SOL_CAN_RAW, CAN_RAW_FILTER, rfilter, sizeof(rfilter));
	strcpy(ifr.ifr_name, device);
	ioctl(con->canfd, SIOCGIFINDEX, &ifr); /* ifr.ifr_ifindex gets filled */
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	err = bind(con->canfd, (struct sockaddr*)&addr, sizeof(addr));
	if (err < 0){
		napi_throw_error(env, NULL, "Cannot bind socket");
		close(con->canfd);
		return g_napi_null;
	}

	/* Init the SDO queue */	
	co_sdo_queue_reset(&con->sdo_queue);

	/* Handle data for SDO and PDO */
	status = napi_get_uv_event_loop(env, &loop);
	napi_assert(env, status);
	uv_poll_init(loop, &con->sdo_uvp, con->canfd);
	con->sdo_uvp.data = con;
	uv_poll_start(&con->sdo_uvp, UV_READABLE, co_recv_cb);

	/* Handle timeout for SDO */
	uv_timer_init(loop, &con->sdo_uvt);
	con->sdo_uvt.data = con;
	con->wait_time = 500; /* Set to 500 ms */

	/* No callback for PDO yet */
	con->cb_pdo_ref = NULL;

	return object;
}

napi_value Init(napi_env env, napi_value exports) {
	napi_status status;
	napi_value tmp;

	status = napi_get_null(env, &g_napi_null);
	if (status != napi_ok) {
		napi_throw_last_error(env);
		return exports;
	}

	status = napi_create_function(env, NULL, 0, wrapper_co_create_node, NULL, &tmp);
	napi_assert(env, status);
	status = napi_set_named_property(env, exports, "create_node", tmp);
	napi_assert(env, status);

/* -- Constants are set in Javascript now --
	status = napi_create_uint32(env, CO_NMT_OPERATIONAL, &tmp);
	napi_assert(env, status);
	status = napi_set_named_property(env, exports, "NMT_OPERATIONAL", tmp);
	napi_assert(env, status);

	status = napi_create_uint32(env, CO_NMT_STOP, &tmp);
	napi_assert(env, status);
	status = napi_set_named_property(env, exports, "NMT_STOP", tmp);
	napi_assert(env, status);

	status = napi_create_uint32(env, CO_NMT_PRE_OPERATIONAL, &tmp);
	napi_assert(env, status);
	status = napi_set_named_property(env, exports, "NMT_PRE_OPERATIONAL", tmp);
	napi_assert(env, status);

	status = napi_create_uint32(env, CO_NMT_RESET_NODE, &tmp);
	napi_assert(env, status);
	status = napi_set_named_property(env, exports, "NMT_RESET_NODE", tmp);
	napi_assert(env, status);

	status = napi_create_uint32(env, CO_NMT_RESET_COMMUNICATION, &tmp);
	napi_assert(env, status);
	status = napi_set_named_property(env, exports, "NMT_RESET_COMMUNICATION", tmp);
	napi_assert(env, status);
*/
	return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)

