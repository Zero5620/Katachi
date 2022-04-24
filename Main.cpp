﻿#include "Kr/KrBasic.h"
#include "Network.h"
#include "Discord.h"
#include "Kr/KrString.h"

#include <stdio.h>
#include <stdlib.h>

#include "Json.h"

//
// https://discord.com/developers/docs/topics/rate-limits#rate-limits
//

static const String Message = u8R"foo(
{
  "content": "Hello, Discord!🔥🔥🔥🔥🔥",
  "tts": false,
  "embeds": [{
    "title": "Hello, Embed!",
    "description": "This is an embedded message."
  }]
}
)foo";

void LogProcedure(void *context, Log_Level level, const char *source, const char *fmt, va_list args) {
	FILE *fp = level == LOG_LEVEL_INFO ? stdout : stderr;
	fprintf(fp, "[%s] ", source);
	vfprintf(fp, fmt, args);
	fprintf(fp, "\n");
}

//
//
//

struct Builder {
	uint8_t *     mem;
	ptrdiff_t     written;
	ptrdiff_t     cap;
	ptrdiff_t     thrown;
};

void BuilderBegin(Builder *builder, void *mem, ptrdiff_t cap) {
	builder->mem         = (uint8_t *)mem;
	builder->written     = 0;
	builder->thrown      = 0;
	builder->cap         = cap;
}

String BuilderEnd(Builder *builder) {
	String buffer(builder->mem, builder->written);
	return buffer;
}

void BuilderWriteBytes(Builder *builder, void *ptr, ptrdiff_t size) {
	uint8_t *data = (uint8_t *)ptr;
	while (size > 0) {
		if (builder->written < builder->cap) {
			ptrdiff_t write_size = Minimum(size, builder->cap - builder->written);
			memcpy(builder->mem + builder->written, data, write_size);
			builder->written += write_size;
			size -= write_size;
			data += write_size;
		} else {
			builder->thrown += size;
			WriteLogErrorEx("Builder", "Buffer full. Failed to write %d bytes", size);
			return;
		}
	}
}

void BuilderWrite(Builder *builder, Buffer buffer) {
	BuilderWriteBytes(builder, buffer.data, buffer.length);
}

template <typename ...Args>
void BuilderWrite(Builder *builder, Buffer buffer, Args... args) {
	BuilderWriteBytes(builder, buffer.data, buffer.length);
	BuilderWrite(builder, args...);
}

void BuilderWriteArray(Builder *builder, Array_View<Buffer> buffers) {
	for (auto b : buffers)
		BuilderWriteBytes(builder, b.data, b.length);
}

//
//
//

#define IsNumber(a) ((a) >= '0' && (a) <= '9')

bool ParseInt(String content, ptrdiff_t *value) {
	ptrdiff_t result = 0;
	ptrdiff_t index = 0;
	for (; index < content.length && IsNumber(content[index]); ++index)
		result = (result * 10 + (content[index] - '0'));
	*value = result;
	return index == content.length;
}

bool ParseHex(String content, ptrdiff_t *value) {
	ptrdiff_t result = 0;
	ptrdiff_t index = 0;
	for (; index < content.length; ++index) {
		ptrdiff_t digit;
		uint8_t ch = content[index];
		if (IsNumber(ch))
			digit = ch - '0';
		else if (ch >= 'a' && ch <= 'f')
			digit = 10 + (ch - 'a');
		else if (ch >= 'A' && ch <= 'F')
			digit = 10 + (ch - 'A');
		else
			break;
		result = (result * 16 + digit);
	}
	*value = result;
	return index == content.length;
}

//
// https://discord.com
//


struct Url {
	String scheme;
	String host;
	String port;
};

static bool Http_UrlExtract(String hostname, Url *url) {
	ptrdiff_t host = StrFind(hostname, "://", 0);
	if (host >= 0) {
		url->scheme     = SubStr(hostname, 0, host);
		ptrdiff_t colon = StrFind(hostname, ":", host + 1);
		if (colon >= 0) {
			url->host = SubStr(hostname, host + 3, colon - host - 3);
			url->port = SubStr(hostname, colon + 1);
		} else {
			url->host = SubStr(hostname, host + 3);
			url->port = url->scheme;
		}
	} else if (hostname.length) {
		url->host   = hostname;
		url->scheme = "http";
		url->port   = url->scheme;
	} else {
		return false;
	}
	return true;
}

enum Http_Connection {
	HTTP_DEFAULT,
	HTTP_CONNECTION,
	HTTPS_CONNECTION
};

static constexpr int HTTP_MAX_HEADER_SIZE = KiloBytes(8);
static constexpr int HTTP_READ_CHUNK_SIZE = HTTP_MAX_HEADER_SIZE;

static_assert(HTTP_MAX_HEADER_SIZE >= HTTP_READ_CHUNK_SIZE, "");

Net_Socket *Http_Connect(const String hostname, Http_Connection connection = HTTP_DEFAULT, Memory_Allocator allocator = ThreadContext.allocator) {
	Url url;
	if (Http_UrlExtract(hostname, &url)) {
		Net_Socket *http = Net_OpenConnection(url.host, url.port, NET_SOCKET_TCP, allocator);
		if (http) {
			if ((connection == HTTP_DEFAULT && url.scheme == "https") || connection == HTTPS_CONNECTION) {
				if (Net_OpenSecureChannel(http, true))
					return http;
				Net_CloseConnection(http);
				return nullptr;
			}
			return http;
		}
		return nullptr;
	}
	WriteLogErrorEx("Http", "Invalid hostname: %*.s", (int)hostname.length, hostname.data);
	return nullptr;
}

void Http_Disconnect(Net_Socket *http) {
	Net_CloseConnection(http);
}

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

static const String HttpHeaderMap[] = {
	"Cache-Control",
	"Connection",
	"Date",
	"Keep-Alive",
	"Pragma",
	"Trailer",
	"Transfer-Encoding",
	"Upgrade",
	"Via",
	"Warning",
	"Allow",
	"Content-Length",
	"Content-Type",
	"Content-Encoding",
	"Content-Language",
	"Content-Location",
	"Content-Md5",
	"Content-Range",
	"Expires",
	"Last-Modified",
	"Accept",
	"Accept-Charset",
	"Accept-Encoding",
	"Accept-Language",
	"Authorization",
	"Cookie",
	"Expect",
	"From",
	"Host",
	"If-Match",
	"If-Modified-Since",
	"If-None-Match",
	"If-Range",
	"If-Unmodified-Since",
	"Max-Forwards",
	"Proxy-Authorization",
	"Referer",
	"Range",
	"Te",
	"Translate",
	"User-Agent",
	"Request-Maximum",
	"Accept-Ranges",
	"Age",
	"Etag",
	"Location",
	"Proxy-Authenticate",
	"Retry-After",
	"Server",
	"Setcookie",
	"Vary",
	"WWW-Authenticate",
	"Response-Maximum",
	"Maximum",
};

static_assert(_HTTP_HEADER_COUNT == ArrayCount(HttpHeaderMap), "");

struct Http_Header_Raw {
	String           name;
	String           value;
	Http_Header_Raw *next;
};

struct Http_Header {
	String known[_HTTP_HEADER_COUNT];
	Http_Header_Raw *raw;
};

typedef Buffer(*Http_Body_Reader_Proc)(void *content);

struct Http_Body_Reader {
	Http_Body_Reader_Proc proc;
	void *context;
};

typedef void(*Http_Body_Writer_Proc)(const Http_Header &header, uint8_t *buffer, int length, void *context);

struct Http_Body_Writer {
	Http_Body_Writer_Proc proc;
	void *context;
};

struct Http_Request {
	struct Body {
		enum Type { STATIC, STREAM };
		struct Data {
			Http_Body_Reader reader;
			Buffer           buffer;
			Data() {}
		};
		Type type;
		Data data;
	};

	Http_Header      headers;
	Body             body;
	Temporary_Memory memory;
};

Http_Request *Http_CreateRequest(Net_Socket *http, Memory_Arena *arena) {
	Temporary_Memory memory              = BeginTemporaryMemory(arena);
	Http_Request *req                    = PushTypeZero(arena, Http_Request);
	req->memory                          = memory;
	req->headers.known[HTTP_HEADER_HOST] = Net_GetHostname(http);
	return req;
}

void Http_DestroyRequest(Http_Request *req) {
	EndTemporaryMemory(&req->memory);
}

Memory_Arena *Http_RequestGetArena(Http_Request *req) {
	return req->memory.arena;
}

void Http_RequestSet(Http_Request *req, Http_Header_Id id, const String value) {
	req->headers.known[id] = value;
}

void Http_RequestSetContentLength(Http_Request *req, ptrdiff_t length) {
	String content_len = FmtStr(req->memory.arena, "%lld", length);
	Http_RequestSet(req, HTTP_HEADER_CONTENT_LENGTH, content_len);
}

void Http_RequestSetContent(Http_Request *req, const String type, const Buffer content) {
	req->body.type        = Http_Request::Body::STATIC;
	req->body.data.buffer = content;
	Http_RequestSet(req, HTTP_HEADER_CONTENT_TYPE, type);
	Http_RequestSetContentLength(req, content.length);
}

void Http_RequestSetReader(Http_Request *req, const String type, Http_Body_Reader_Proc reader, void *context) {
	req->body.type                = Http_Request::Body::STREAM;
	req->body.data.reader.proc    = reader;
	req->body.data.reader.context = context;
	Http_RequestSet(req, HTTP_HEADER_CONTENT_TYPE, type);
}

bool Http_RequestAddCustomHeader(Http_Request *req, String name, String value) {
	Http_Header_Raw *raw = PushType(req->memory.arena, Http_Header_Raw);
	if (raw) {
		raw->name        = name;
		raw->value       = value;
		raw->next        = req->headers.raw;
		req->headers.raw = raw;
		return true;
	}
	return false;
}

enum Http_Version : uint32_t {
	HTTP_VERSION_1_0,
	HTTP_VERSION_1_1,
};

struct Http_Status {
	Http_Version version;
	uint32_t     code;
	String       name;
};

struct Http_Response {
	Http_Status      status;
	Http_Header      headers;
	Buffer           body;
	Temporary_Memory memory;
};

Http_Response *Http_CreateResponse(Memory_Arena *arena) {
	Temporary_Memory memory = BeginTemporaryMemory(arena);
	Http_Response *res      = PushTypeZero(arena, Http_Response);
	res->memory             = memory;
	return res;
}

void Http_DestroyResponse(Http_Response *req) {
	EndTemporaryMemory(&req->memory);
}

Memory_Arena *Http_ResponseGetArena(Http_Response *res) {
	return res->memory.arena;
}

bool Http_ResponseAddCustomHeader(Http_Response *res, String name, String value) {
	Http_Header_Raw *raw = PushType(res->memory.arena, Http_Header_Raw);
	if (raw) {
		raw->name        = name;
		raw->value       = value;
		raw->next        = res->headers.raw;
		res->headers.raw = raw;
		return true;
	}
	return false;
}

static inline bool Http_WriteBytes(Net_Socket *http, uint8_t *bytes, ptrdiff_t bytes_to_write) {
	while (bytes_to_write > 0) {
		int bytes_sent = Net_Write(http, bytes, (int)bytes_to_write);
		if (bytes_sent > 0) {
			bytes += bytes_sent;
			bytes_to_write -= bytes_sent;
			continue;
		}
		return false;
	}
	return true;
}

static inline Http_Response *Http_ResponseFailed(Http_Response * res, const char *fmt, ...) {
	if (fmt) {
		va_list args;
		va_start(args, fmt);
		WriteLogErrorExV("Http", fmt, args);
		va_end(args);
	}
	Http_DestroyResponse(res);
	return nullptr;
}

static inline bool Http_ProcessReceivedBuffer(const Http_Header &header, uint8_t *buffer, int length, Memory_Arena *arena, Http_Body_Writer *writer) {
	if (!writer) {
		uint8_t *dst = (uint8_t *)PushSize(arena, length);
		if (dst) {
			memcpy(dst, buffer, length);
			return true;
		}
		return false;
	}
	writer->proc(header, buffer, length, writer->context);
	return true;
}

Http_Response *Http_SendCustomMethod(Net_Socket *http, const String method, const String endpoint, Http_Request *req, Http_Body_Writer *writer = nullptr) {
	Memory_Arena *arena = Http_RequestGetArena(req);

	{
		// Send Request
		Defer{ Http_DestroyRequest(req); };

		void *mem = PushSize(arena, HTTP_MAX_HEADER_SIZE);
		if (!mem) {
			WriteLogErrorEx("Http", "Send request failed: Out of Memory");
			return nullptr;
		}

		Builder builder;
		BuilderBegin(&builder, mem, HTTP_MAX_HEADER_SIZE);
		BuilderWrite(&builder, method, String(" "), endpoint, String(" HTTP/1.1\r\n"));

		for (int id = 0; id < _HTTP_HEADER_COUNT; ++id) {
			String value = req->headers.known[id];
			if (value.length) {
				BuilderWrite(&builder, HttpHeaderMap[id], String(":"), value, String("\r\n"));
			}
		}

		for (auto raw = req->headers.raw; raw; raw = raw->next)
			BuilderWrite(&builder, raw->name, String(":"), raw->value, String("\r\n"));

		BuilderWrite(&builder, "\r\n");

		if (builder.thrown)
			return nullptr;

		String buffer = BuilderEnd(&builder);
		if (!Http_WriteBytes(http, buffer.data, buffer.length))
			return nullptr;

		if (req->body.type == Http_Request::Body::STATIC) {
			if (!Http_WriteBytes(http, req->body.data.buffer.data, req->body.data.buffer.length))
				return nullptr;
		} else {
			Http_Body_Reader reader = req->body.data.reader;
			while (true) {
				Buffer buffer = reader.proc(reader.context);
				if (!buffer.length) break;
				if (!Http_WriteBytes(http, buffer.data, buffer.length))
					return nullptr;
			}
		}
	}

	uint8_t stream_buffer[HTTP_READ_CHUNK_SIZE];

	Http_Response *res = Http_CreateResponse(arena);

	String raw_header;
	String raw_body_part;

	{
		// Read Header
		uint8_t *read_ptr  = stream_buffer;
		int buffer_size    = HTTP_MAX_HEADER_SIZE;
		uint8_t *parse_ptr = read_ptr;

		bool read_more = true;

		while (read_more) {
			int bytes_read = Net_Read(http, read_ptr, buffer_size);
			if (bytes_read <= 0) return Http_ResponseFailed(res, nullptr);

			read_ptr += bytes_read;
			buffer_size -= bytes_read;

			while (read_ptr - parse_ptr > 1) {
				String part(parse_ptr, read_ptr - parse_ptr);
				ptrdiff_t pos = StrFind(part, "\r\n");
				if (pos < 0) {
					if (buffer_size)
						break;
					return Http_ResponseFailed(res, "Reading header failed: Out of Memory");
				}

				parse_ptr += pos + 2;

				if (pos == 0) {
					ptrdiff_t header_size = parse_ptr - stream_buffer;
					uint8_t *header_mem   = (uint8_t *)PushSize(arena, header_size);
					if (!header_mem) return Http_ResponseFailed(res, "Reading header failed: Out of Memory");

					memcpy(header_mem, stream_buffer, header_size);

					raw_header    = String(header_mem, header_size);
					raw_body_part = String(parse_ptr, read_ptr - parse_ptr);
					read_more     = false;
					break;
				}
			}
		}
	}

	{
		// Parse headers
		uint8_t *trav = raw_header.data;
		uint8_t *last = trav + raw_header.length;

		enum { PARSISNG_STATUS, PARSING_FIELDS };

		int state = PARSISNG_STATUS;

		while (true) {
			String part(trav, last - trav);
			ptrdiff_t pos = StrFind(part, "\r\n");
			if (pos == 0) // Finished
				break;

			if (pos < 0)
				return Http_ResponseFailed(res, "Invalid header received");

			String line(trav, pos);
			trav += pos + 2;

			if (state == PARSING_FIELDS) {
				ptrdiff_t colon = StrFindCharacter(line, ':');
				if (colon <= 0) return Http_ResponseFailed(res, "Invalid header received");

				String name = SubStr(line, 0, colon);
				name = StrTrim(name);
				String value = SubStr(line, colon + 1);
				value = StrTrim(value);

				bool known_header = false;
				for (int index = 0; index < _HTTP_HEADER_COUNT; ++index) {
					if (name == HttpHeaderMap[index]) {
						res->headers.known[index] = value;
						known_header = true;
						break;
					}
				}

				if (!known_header) {
					if (!Http_ResponseAddCustomHeader(res, name, value))
						return Http_ResponseFailed(res, "Reading header failed: Out of Memory");
				}
			} else {
				const String prefixes[] = { "HTTP/1.1 ", "HTTP/1.0 " };
				constexpr Http_Version versions[] = { HTTP_VERSION_1_1, HTTP_VERSION_1_0 };
				static_assert(ArrayCount(prefixes) == ArrayCount(versions), "");

				bool prefix_present = false;
				for (int index = 0; index < ArrayCount(prefixes); ++index) {
					String prefix = prefixes[index];
					if (StrStartsWith(line, prefix)) {
						line = StrRemovePrefix(line, prefix.length);
						res->status.version = versions[index];
						prefix_present = true;
						break;
					}
				}
				if (!prefix_present)
					return Http_ResponseFailed(res, "Invalid header received");

				line = StrTrim(line);
				ptrdiff_t name_pos = StrFindCharacter(line, ' ');
				if (name_pos < 0)
					return Http_ResponseFailed(res, "Invalid header received");

				ptrdiff_t status_code;
				if (!ParseInt(SubStr(line, 0, name_pos), &status_code))
					return Http_ResponseFailed(res, "Invalid header received");
				else if (status_code < 0)
					return Http_ResponseFailed(res, "Invalid status code received");
				res->status.code = (uint32_t)status_code;
				res->status.name = SubStr(line, name_pos + 1);

				state = PARSING_FIELDS;
			}
		}
	}

	// Body: Content-Length
	const String content_length_value = res->headers.known[HTTP_HEADER_CONTENT_LENGTH];
	if (content_length_value.length) {
		ptrdiff_t content_length = 0;
		if (!ParseInt(content_length_value, &content_length))
			return Http_ResponseFailed(res, "Invalid Content-Length in the header");

		if (content_length < raw_body_part.length)
			return Http_ResponseFailed(res, "Invalid Content-Length in the header");

		Assert(raw_body_part.length < HTTP_READ_CHUNK_SIZE);

		if (!writer) {
			res->body.data   = (uint8_t *)MemoryArenaGetCurrent(arena);
			res->body.length = content_length;
		}

		if (!Http_ProcessReceivedBuffer(res->headers, raw_body_part.data, (int)raw_body_part.length, arena, writer))
			return Http_ResponseFailed(res, nullptr);

		ptrdiff_t remaining = content_length - raw_body_part.length;
		while (remaining) {
			int bytes_read = Net_Read(http, stream_buffer, (int)Minimum(remaining, HTTP_READ_CHUNK_SIZE));
			if (bytes_read <= 0) return Http_ResponseFailed(res, nullptr);
			if (!Http_ProcessReceivedBuffer(res->headers, stream_buffer, bytes_read, arena, writer))
				return Http_ResponseFailed(res, nullptr);
			remaining -= bytes_read;
		}
	} else {
		String transfer_encoding = res->headers.known[HTTP_HEADER_TRANSFER_ENCODING];

		// Transfer-Encoding: chunked
		if (StrFind(transfer_encoding, "chunked") >= 0) {
			Assert(raw_body_part.length < HTTP_READ_CHUNK_SIZE);

			memmove(stream_buffer, raw_body_part.data, raw_body_part.length);

			if (!writer) {
				res->body.data = (uint8_t *)MemoryArenaGetCurrent(arena);
			}

			ptrdiff_t body_length = 0;
			ptrdiff_t chunk_read  = raw_body_part.length;

			while (true) {
				while (chunk_read < 2) {
					int bytes_read = Net_Read(http, stream_buffer + chunk_read, (HTTP_READ_CHUNK_SIZE - (int)chunk_read));
					if (bytes_read <= 0) return Http_ResponseFailed(res, nullptr);
					chunk_read += bytes_read;
				}

				String chunk_header(stream_buffer, chunk_read);
				ptrdiff_t data_pos = StrFind(chunk_header, "\r\n");
				if (data_pos < 0)
					return Http_ResponseFailed(res, "Reading header failed: chuck size not found");

				String length_str = SubStr(chunk_header, 0, data_pos);
				ptrdiff_t chunk_length;
				if (!ParseHex(length_str, &chunk_length) || chunk_length < 0)
					return Http_ResponseFailed(res, "Reading header failed: Invalid chuck size");

				if (chunk_length == 0)
					break;

				data_pos += 2; // skip \r\n
				ptrdiff_t streamed_chunk_len = chunk_read - data_pos;

				if (chunk_length <= streamed_chunk_len) {
					if (!Http_ProcessReceivedBuffer(res->headers, stream_buffer + data_pos, (int)chunk_length, arena, writer))
						return Http_ResponseFailed(res, nullptr);
					chunk_read -= (data_pos + chunk_length);
					memmove(stream_buffer, stream_buffer + data_pos + chunk_length, chunk_read);
				} else {
					if (!Http_ProcessReceivedBuffer(res->headers, stream_buffer + data_pos, (int)streamed_chunk_len, arena, writer))
						return Http_ResponseFailed(res, nullptr);

					ptrdiff_t remaining = chunk_length - streamed_chunk_len;
					while (remaining) {
						int bytes_read = Net_Read(http, stream_buffer, (int)Minimum(remaining, HTTP_READ_CHUNK_SIZE));
						if (bytes_read <= 0) return Http_ResponseFailed(res, nullptr);
						if (!Http_ProcessReceivedBuffer(res->headers, stream_buffer, bytes_read, arena, writer))
							return Http_ResponseFailed(res, nullptr);
						remaining -= bytes_read;
					}

					chunk_read = 0;
				}

				body_length += chunk_length;
			}
			if (!writer)
				res->body.length = body_length;
		}
	}

	return res;
}

Http_Response *Http_Get(Net_Socket *http, const String endpoint, Http_Request *req, Http_Body_Writer *writer = nullptr) {
	return Http_SendCustomMethod(http, "GET", endpoint, req, writer);
}

Http_Response *Http_Post(Net_Socket *http, const String endpoint, Http_Request *req, Http_Body_Writer *writer = nullptr) {
	return Http_SendCustomMethod(http, "POST", endpoint, req, writer);
}

Http_Response *Http_Put(Net_Socket *http, const String endpoint, Http_Request *req, Http_Body_Writer *writer = nullptr) {
	return Http_SendCustomMethod(http, "PUT", endpoint, req, writer);
}

void Dump(const Http_Header &header, uint8_t *buffer, int length, void *content) {
	printf("%.*s", length, buffer);
}

int main(int argc, char **argv) {
	InitThreadContext(KiloBytes(64));
	ThreadContextSetLogger({ LogProcedure, nullptr });

	if (argc != 2) {
		fprintf(stderr, "USAGE: %s token\n\n", argv[0]);
		return 1;
	}

	Net_Initialize();

	Memory_Arena *arena = MemoryArenaAllocate(MegaBytes(64));

	Net_Socket *http = Http_Connect("https://discord.com");
	if (!http) {
		return 1;
	}

	const String token = FmtStr(arena, "Bot %s", argv[1]);

	Http_Request *req = Http_CreateRequest(http, arena);
	Http_RequestSet(req, HTTP_HEADER_AUTHORIZATION, token);
	Http_RequestSet(req, HTTP_HEADER_USER_AGENT, "Katachi");
	Http_RequestSet(req, HTTP_HEADER_CONNECTION, "keep-alive");
	Http_RequestSetContent(req, "application/json", Message);

	Http_Body_Writer writer = { Dump };

	Http_Response *res = Http_Post(http, "/api/v9/channels/850062914438430751/messages", req);
	
	if (res) {
		printf("%.*s\n\n", (int)res->body.length, res->body.data);

		Http_DestroyResponse(res);
	}

	Http_Disconnect(http);


#if 0
	Net_Socket *net = Net_OpenConnection("discord.com", "443", NET_SOCKET_TCP);
	if (!net) {
		return 1;
	}

	Net_OpenSecureChannel(net, false);


	String_Builder builder;

	String method = "POST";
	String endpoint = "/api/v9/channels/850062383266136065/messages";

	WriteFormatted(&builder, "% % HTTP/1.1\r\n", method, endpoint);
	WriteFormatted(&builder, "Authorization: Bot %\r\n", token);
	Write(&builder, "User-Agent: KatachiBot\r\n");
	Write(&builder, "Connection: keep-alive\r\n");
	WriteFormatted(&builder, "Host: %\r\n", Net_GetHostname(net));
	WriteFormatted(&builder, "Content-Type: %\r\n", "application/json");
	WriteFormatted(&builder, "Content-Length: %\r\n", Message.length);
	Write(&builder, "\r\n");
	WriteBuffer(&builder, Message.data, Message.length);

	// Send header
	int bytes_sent = 0;
	for (auto buk = &builder.head; buk; buk = buk->next) {
		bytes_sent += Net_Write(net, buk->data, (int)buk->written);
	}

	static char buffer[4096 * 2];

	int bytes_received = 0;
	bytes_received += Net_Read(net, buffer, sizeof(buffer) - 1);
	bytes_received += Net_Read(net, buffer + bytes_received, sizeof(buffer) - 1 - bytes_received);
	if (bytes_received < 1) {
		Unimplemented();
	}

	buffer[bytes_received] = 0;

	printf("\n%s\n", buffer);


	Net_CloseConnection(net);
#endif

	Net_Shutdown();

	return 0;
}
