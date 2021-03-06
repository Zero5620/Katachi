#pragma once
#include "Network.h"

static constexpr int HTTP_MAX_HEADER_SIZE   = KiloBytes(8);
static constexpr int HTTP_STREAM_CHUNK_SIZE = HTTP_MAX_HEADER_SIZE;
static constexpr int HTTP_MAX_RAW_HEADERS   = 64;
static constexpr int HTTP_MAX_QUERY_PARAMS  = 8;

static_assert(HTTP_MAX_HEADER_SIZE >= HTTP_STREAM_CHUNK_SIZE, "");

enum Http_Connection {
	HTTP_DEFAULT,
	HTTP_CONNECTION,
	HTTPS_CONNECTION
};

enum Http_Header_Id {
	HTTP_HEADER_CACHE_CONTROL,
	HTTP_HEADER_CONNECTION,
	HTTP_HEADER_DATE,
	HTTP_HEADER_KEEP_ALIVE,
	HTTP_HEADER_PRAGMA,
	HTTP_HEADER_TRAILER,
	HTTP_HEADER_TRANSFER_ENCODING,
	HTTP_HEADER_UPGRADE,
	HTTP_HEADER_VIA,
	HTTP_HEADER_WARNING,
	HTTP_HEADER_ALLOW,
	HTTP_HEADER_CONTENT_LENGTH,
	HTTP_HEADER_CONTENT_TYPE,
	HTTP_HEADER_CONTENT_ENCODING,
	HTTP_HEADER_CONTENT_LANGUAGE,
	HTTP_HEADER_CONTENT_LOCATION,
	HTTP_HEADER_CONTENT_MD5,
	HTTP_HEADER_CONTENT_RANGE,
	HTTP_HEADER_EXPIRES,
	HTTP_HEADER_LAST_MODIFIED,
	HTTP_HEADER_ACCEPT,
	HTTP_HEADER_ACCEPT_CHARSET,
	HTTP_HEADER_ACCEPT_ENCODING,
	HTTP_HEADER_ACCEPT_LANGUAGE,
	HTTP_HEADER_AUTHORIZATION,
	HTTP_HEADER_COOKIE,
	HTTP_HEADER_EXPECT,
	HTTP_HEADER_FROM,
	HTTP_HEADER_HOST,
	HTTP_HEADER_IF_MATCH,
	HTTP_HEADER_IF_MODIFIED_SINCE,
	HTTP_HEADER_IF_NONE_MATCH,
	HTTP_HEADER_IF_RANGE,
	HTTP_HEADER_IF_UNMODIFIED_SINCE,
	HTTP_HEADER_MAX_FORWARDS,
	HTTP_HEADER_PROXY_AUTHORIZATION,
	HTTP_HEADER_REFERER,
	HTTP_HEADER_RANGE,
	HTTP_HEADER_TE,
	HTTP_HEADER_TRANSLATE,
	HTTP_HEADER_USER_AGENT,
	HTTP_HEADER_REQUEST_MAXIMUM,
	HTTP_HEADER_ACCEPT_RANGES,
	HTTP_HEADER_AGE,
	HTTP_HEADER_ETAG,
	HTTP_HEADER_LOCATION,
	HTTP_HEADER_PROXY_AUTHENTICATE,
	HTTP_HEADER_RETRY_AFTER,
	HTTP_HEADER_SERVER,
	HTTP_HEADER_SETCOOKIE,
	HTTP_HEADER_VARY,
	HTTP_HEADER_WWW_AUTHENTICATE,
	HTTP_HEADER_RESPONSE_MAXIMUM,
	HTTP_HEADER_MAXIMUM,

	_HTTP_HEADER_COUNT
};

struct Http_Raw_Headers {
	struct Header {
		String name;
		String value;
	};

	ptrdiff_t count;
	Header    data[HTTP_MAX_RAW_HEADERS];
};

struct Http_Header {
	String known[_HTTP_HEADER_COUNT];
	Http_Raw_Headers raw;
};

struct Http_Query {
	String name;
	String value;
};

struct Http_Query_Params {
	ptrdiff_t  count = 0;
	Http_Query queries[HTTP_MAX_QUERY_PARAMS];
};

enum Http_Version : uint32_t {
	HTTP_VERSION_1_1,
	HTTP_VERSION_1_0,
};

struct Http_Status {
	Http_Version version;
	uint32_t     code;
	String       name;
};

struct Http_Request {
	Http_Version version;
	Http_Header  headers;
	ptrdiff_t    length;
	uint8_t      buffer[HTTP_MAX_HEADER_SIZE];
	Buffer       body;
};

struct Http_Response {
	Http_Status  status;
	Http_Header  headers;
	ptrdiff_t    length;
	uint8_t      buffer[HTTP_MAX_HEADER_SIZE];
	Buffer       body;
};

typedef int(*Http_Reader_Proc)(uint8_t *buffer, int length, void *context);
typedef void(*Http_Writer_Proc)(Http_Header &header, uint8_t *buffer, ptrdiff_t length, void *context);

struct Http_Reader {
	Http_Reader_Proc proc;
	void *           context;
};

struct Http_Writer {
	Http_Writer_Proc proc;
	void *           context;
};

struct Http;

Net_Socket *Http_GetSocket(Http *http);
Http *      Http_FromSocket(Net_Socket *socket);

Http *Http_Connect(const String host, const String port, Http_Connection connection, Memory_Allocator allocator);
Http *Http_Connect(const String hostname, Http_Connection connection = HTTP_DEFAULT, Memory_Allocator allocator = ThreadContext.allocator);
bool  Http_Reconnect(Http *http);
void  Http_Disconnect(Http *http);

void      Http_DumpProc(Http_Header &header, uint8_t *buffer, ptrdiff_t length, void *context);
ptrdiff_t Http_BuildRequest(const String method, const String endpoint, const Http_Query_Params *params, const Http_Request &req, uint8_t *buffer, ptrdiff_t buff_len);
bool      Http_SendRequest(Http *http, const String header, Http_Reader reader);
bool      Http_ReceiveResponse(Http *http, Http_Response *res, Http_Writer writer);

bool Http_CustomMethod(Http *http, const String method, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Reader reader, Http_Response *res, Http_Writer writer);
bool Http_Post(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Reader reader, Http_Response *res, Http_Writer writer);
bool Http_Get(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Reader reader, Http_Response *res, Http_Writer writer);
bool Http_Put(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Reader reader, Http_Response *res, Http_Writer writer);

bool Http_CustomMethod(Http *http, const String method, const String endpoint, const Http_Request &req, Http_Reader reader, Http_Response *res, Http_Writer writer);
bool Http_Post(Http *http, const String endpoint, const Http_Request &req, Http_Reader reader, Http_Response *res, Http_Writer writer);
bool Http_Get(Http *http, const String endpoint, const Http_Request &req, Http_Reader reader, Http_Response *res, Http_Writer writer);
bool Http_Put(Http *http, const String endpoint, const Http_Request &req, Http_Reader reader, Http_Response *res, Http_Writer writer);

bool Http_CustomMethod(Http *http, const String method, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Response *res, Http_Writer writer);
bool Http_Post(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Response *res, Http_Writer writer);
bool Http_Get(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Response *res, Http_Writer writer);
bool Http_Put(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Response *res, Http_Writer writer);

bool Http_CustomMethod(Http *http, const String method, const String endpoint, const Http_Request &req, Http_Response *res, Http_Writer writer);
bool Http_Post(Http *http, const String endpoint, const Http_Request &req, Http_Response *res, Http_Writer writer);
bool Http_Get(Http *http, const String endpoint, const Http_Request &req, Http_Response *res, Http_Writer writer);
bool Http_Put(Http *http, const String endpoint, const Http_Request &req, Http_Response *res, Http_Writer writer);

bool Http_CustomMethod(Http *http, const String method, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Reader reader, Http_Response *res, Memory_Arena *arena);
bool Http_Post(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Reader reader, Http_Response *res, Memory_Arena *arena);
bool Http_Get(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Reader reader, Http_Response *res, Memory_Arena *arena);
bool Http_Put(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Reader reader, Http_Response *res, Memory_Arena *arena);

bool Http_CustomMethod(Http *http, const String method, const String endpoint, const Http_Request &req, Http_Reader reader, Http_Response *res, Memory_Arena *arena);
bool Http_Post(Http *http, const String endpoint, const Http_Request &req, Http_Reader reader, Http_Response *res, Memory_Arena *arena);
bool Http_Get(Http *http, const String endpoint, const Http_Request &req, Http_Reader reader, Http_Response *res, Memory_Arena *arena);
bool Http_Put(Http *http, const String endpoint, const Http_Request &req, Http_Reader reader, Http_Response *res, Memory_Arena *arena);

bool Http_CustomMethod(Http *http, const String method, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Response *res, Memory_Arena *arena);
bool Http_Post(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Response *res, Memory_Arena *arena);
bool Http_Get(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Response *res, Memory_Arena *arena);
bool Http_Put(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Response *res, Memory_Arena *arena);

bool Http_CustomMethod(Http *http, const String method, const String endpoint, const Http_Request &req, Http_Response *res, Memory_Arena *arena);
bool Http_Post(Http *http, const String endpoint, const Http_Request &req, Http_Response *res, Memory_Arena *arena);
bool Http_Get(Http *http, const String endpoint, const Http_Request &req, Http_Response *res, Memory_Arena *arena);
bool Http_Put(Http *http, const String endpoint, const Http_Request &req, Http_Response *res, Memory_Arena *arena);

bool Http_CustomMethod(Http *http, const String method, const String endpoint, const Http_Request &req, Http_Reader reader, Http_Response *res, uint8_t *memory, ptrdiff_t length);
bool Http_Post(Http *http, const String endpoint, const Http_Request &req, Http_Reader reader, Http_Response *res, uint8_t *memory, ptrdiff_t length);
bool Http_Get(Http *http, const String endpoint, const Http_Request &req, Http_Reader reader, Http_Response *res, uint8_t *memory, ptrdiff_t length);
bool Http_Put(Http *http, const String endpoint, const Http_Request &req, Http_Reader reader, Http_Response *res, uint8_t *memory, ptrdiff_t length);

bool Http_CustomMethod(Http *http, const String method, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Response *res, uint8_t *memory, ptrdiff_t length);
bool Http_Post(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Response *res, uint8_t *memory, ptrdiff_t length);
bool Http_Get(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Response *res, uint8_t *memory, ptrdiff_t length);
bool Http_Put(Http *http, const String endpoint, const Http_Query_Params &params, const Http_Request &req, Http_Response *res, uint8_t *memory, ptrdiff_t length);

bool Http_CustomMethod(Http *http, const String method, const String endpoint, const Http_Request &req, Http_Response *res, uint8_t *memory, ptrdiff_t length);
bool Http_Post(Http *http, const String endpoint, const Http_Request &req, Http_Response *res, uint8_t *memory, ptrdiff_t length);
bool Http_Get(Http *http, const String endpoint, const Http_Request &req, Http_Response *res, uint8_t *memory, ptrdiff_t length);
bool Http_Put(Http *http, const String endpoint, const Http_Request &req, Http_Response *res, uint8_t *memory, ptrdiff_t length);

void   Http_QueryParamSet(Http_Query_Params *params, String name, String value);
String Http_QueryParamGet(Http_Query_Params *params, String name);

void   Http_DumpHeader(const Http_Request &req);
void   Http_DumpHeader(const Http_Response &res);
void   Http_InitRequest(Http_Request *req);
void   Http_SetHost(Http_Request *req, Http *http);
void   Http_SetHeaderFmt(Http_Request *req, Http_Header_Id id, const char *fmt, ...);
void   Http_SetHeaderFmt(Http_Request *req, String name, const char *fmt, ...);
void   Http_SetHeader(Http_Request *req, Http_Header_Id id, String value);
void   Http_SetHeader(Http_Request *req, String name, String value);
void   Http_AppendHeader(Http_Request *req, Http_Header_Id id, String value);
void   Http_AppendHeader(Http_Request *req, String name, String value);
void   Http_SetContentLength(Http_Request *req, ptrdiff_t length);
void   Http_SetContent(Http_Request *req, String type, Buffer content);
void   Http_SetBody(Http_Request *req, Buffer content);
String Http_GetHeader(Http_Request *req, Http_Header_Id id);
String Http_GetHeader(Http_Request *req, const String name);

void   Http_InitResponse(Http_Response *res);
void   Http_SetHeaderFmt(Http_Response *res, Http_Header_Id id, const char *fmt, ...);
void   Http_SetHeaderFmt(Http_Response *res, String name, const char *fmt, ...);
void   Http_SetHeader(Http_Response *res, Http_Header_Id id, String value);
void   Http_SetHeader(Http_Response *res, String name, String value);
void   Http_AppendHeader(Http_Response *res, Http_Header_Id id, String value);
void   Http_AppendHeader(Http_Response *res, String name, String value);
void   Http_SetContentLength(Http_Response *res, ptrdiff_t length);
void   Http_SetContent(Http_Response *res, String type, Buffer content);
void   Http_SetBody(Http_Response *res, Buffer content);
String Http_GetHeader(Http_Response *res, Http_Header_Id id);
String Http_GetHeader(Http_Response *res, const String name);

//
//
//

constexpr int HTTP_MULTIPART_LENGTH = 64;

struct Http_Multipart {
	uint8_t       boundary[HTTP_MULTIPART_LENGTH];
	ptrdiff_t     length = 0;
	uint8_t *memory = nullptr;
	uint8_t *current = nullptr;
	Memory_Arena *arena = nullptr;
};

Http_Multipart Http_MultipartBegin(Memory_Arena *arena);
bool Http_MultipartData(Http_Multipart *mt, String content, String filename);
bool Http_MultipartData(Http_Multipart *mt, String content, String content_type, String filename);
String Http_MultipartEnd(Http_Multipart *mt);
