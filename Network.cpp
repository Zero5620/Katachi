#include "Network.h"

#if PLATFORM_WINDOWS
#include <Winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#elif PLATFORM_LINUX || PLATFORM_MAC
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define SOCKET int
#define INVALID_SOCKET -1
#endif

#ifdef NETWORK_OPENSSL_ENABLE
#include <openssl/ssl.h>
#include <openssl/err.h>
#if PLATFORM_WINDOWS
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "openssl/libcrypto_static.lib")
#pragma comment(lib, "openssl/libssl_static.lib")
#endif
#endif

//
//
//

typedef int(*Net_Write_Proc)(struct Net_Socket *net, void *buffer, int length);
typedef int(*Net_Read_Proc)(struct Net_Socket *net, void *buffer, int length);

struct Net_Socket {
	Net_Write_Proc   write;
	Net_Read_Proc    read;
#ifdef NETWORK_OPENSSL_ENABLE
	SSL *            ssl;
#endif
	SOCKET           descriptor;
	Net_Socket_Type  type;
	String           node;
	String           service;
	Memory_Allocator allocator;
};

//
//
//

static bool Net_Reconnect(Net_Socket *net);

#if PLATFORM_WINDOWS
static wchar_t *PL_Net_UnicodeToWideChar(Memory_Arena *arena, const char *msg, int length) {
	wchar_t *result = (wchar_t *)PushSize(arena, (length + 1) * sizeof(wchar_t));
	int wlen = MultiByteToWideChar(CP_UTF8, 0, msg, length, result, length + 1);
	result[wlen] = 0;
	return result;
}

static void PL_Net_ReportError(int error) {
	DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
	wchar_t *msg = NULL;
	FormatMessageW(flags, NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&msg, 0, NULL);
	WriteLogErrorEx("Net:Windows", "%S", msg);
	LocalFree(msg);
}
#define PL_Net_ReportLastWindowsError() PL_Net_ReportError(GetLastError())
#define PL_Net_ReportLastWinsockError() PL_Net_ReportError(WSAGetLastError())

static void PL_Net_Shutdown() {
	WSACleanup();
}

static bool PL_Net_Initialize() {
	WSADATA wsaData;
	int error = WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (error != 0) {
		PL_Net_ReportError(error);
		return false;
	}
	return true;
}

static SOCKET PL_Net_OpenSocketDescriptor(const String node, const String service, Net_Socket_Type type) {
	static constexpr int SocketTypeMap[] = { SOCK_STREAM, SOCK_DGRAM };

	ADDRINFOW hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SocketTypeMap[type];

	Memory_Arena *scratch = ThreadScratchpad();
	Temporary_Memory temp = BeginTemporaryMemory(scratch);
	Defer{ EndTemporaryMemory(&temp); };

	wchar_t *nodename = PL_Net_UnicodeToWideChar(scratch, (char *)node.data, (int)node.length);
	wchar_t *servicename = PL_Net_UnicodeToWideChar(scratch, (char *)service.data, (int)service.length);

	ADDRINFOW *address = nullptr;
	int error = GetAddrInfoW(nodename, servicename, &hints, &address);
	if (error) {
		PL_Net_ReportError(error);
		return INVALID_SOCKET;
	}

	SOCKET descriptor = INVALID_SOCKET;
	for (auto ptr = address; ptr; ptr = ptr->ai_next) {
		descriptor = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

		if (descriptor == INVALID_SOCKET) {
			FreeAddrInfoW(address);
			PL_Net_ReportLastWinsockError();
			return INVALID_SOCKET;
		}

		error = connect(descriptor, ptr->ai_addr, (int)ptr->ai_addrlen);
		if (error) {
			closesocket(descriptor);
			descriptor = INVALID_SOCKET;
			continue;
		}

		break;
	}

	FreeAddrInfoW(address);

	if (error)
		PL_Net_ReportError(error);

	return descriptor;
}

static void PL_Net_CloseSocketDescriptor(SOCKET descriptor) {
	closesocket(descriptor);
}

static int PL_Net_Write(Net_Socket *net, void *buffer, int length) {
	int written = send((SOCKET)net->descriptor, (char *)buffer, length, 0);

	if (written > 0)
		return written;

	int error = WSAGetLastError();
	if (error == WSAECONNRESET || error == WSAECONNABORTED) {
		if (Net_Reconnect(net)) {
			int written = send((SOCKET)net->descriptor, (char *)buffer, length, 0);
			return written;
		}
	}

	PL_Net_ReportError(error);
	return written;
}

static int PL_Net_Read(Net_Socket *net, void *buffer, int length) {
	int read = recv((SOCKET)net->descriptor, (char *)buffer, length, 0);
	return read;
}

#elif PLATFORM_LINUX || PLATFORM_MAC
static char *PL_Net_StringToCString(Memory_Arena *arena, String src) {
	char *dst = (char *)PushSize(arena, src.length + 1);
	memcpy(dst, src.data, src.length);
	dst[src.length] = 0;
	return dst;
}

static bool PL_Net_Initialize() {
	signal(SIGPIPE, SIG_IGN);
	return true;
}

static Net_Result PL_Net_OpenSocketDescriptor(const String node, const String service, Net_Socket_Type type, int64_t *pdescriptor) {
	static constexpr int SocketTypeMap[] = { SOCK_STREAM, SOCK_DGRAM };

	*pdescriptor = INVALID_SOCKET;

	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SocketTypeMap[type];

	Memory_Arena *scratch = ThreadScratchpad();
	Temporary_Memory temp = BeginTemporaryMemory(scratch);
	Defer{ EndTemporaryMemory(&temp); };

	char *nodename = PL_Net_StringToCString(scratch, node);
	char *servicename = PL_Net_StringToCString(scratch, service);

	addrinfo *address = nullptr;
	int error = getaddrinfo(nodename, servicename, &hints, &address);
	if (error) {
		return PL_Net_ConvertNativeError(error);
	}

	SOCKET descriptor = INVALID_SOCKET;
	for (auto ptr = address; ptr; ptr = ptr->ai_next) {
		descriptor = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

		if (descriptor == INVALID_SOCKET) {
			freeaddrinfo(address);
			return PL_Net_GetLastError();
		}

		error = connect(descriptor, ptr->ai_addr, (int)ptr->ai_addrlen);
		if (error) {
			closesocket(descriptor);
			descriptor = INVALID_SOCKET;
			continue;
		}

		break;
	}

	freeaddrinfo(address);

	*pdescriptor = descriptor;

	if (error)
		return PL_Net_ConvertNativeError(error);

	return NET_OK;
}

static int PL_Net_Write(Net_Socket *net, void *buffer, int length) {
	int written = send((SOCKET)net->descriptor, (char *)buffer, length, 0);

	if (written > 0)
		return written;

	if (errno == EPIPE) {
		if (Net_Reconnect(net)) {
			int written = send((SOCKET)net->descriptor, (char *)buffer, length, 0);
			return written;
		}
	}

	PL_Net_ReportError(error);
	return written;
}

static int PL_Net_Read(Net_Socket *net, void *buffer, int length) {
	int read = recv((SOCKET)net->descriptor, (char *)buffer, length, 0);
	return read;
}
#endif

#ifdef NETWORK_OPENSSL_ENABLE
static SSL_CTX *DefaultClientContext;
static SSL_CTX *DefaultClientVerifyContext;

static void PL_Net_ReportOpenSSLError() {
	char message[1024];
	unsigned long error;

	while (1) {
		error = ERR_get_error();
		if (!error) return;
		ERR_error_string_n(error, message, sizeof(message));
		WriteLogErrorEx("Net:OpenSSL", "%s", message);
	}
}

static void PL_Net_OpenSSLShutdown() {
	SSL_CTX_free(DefaultClientContext);
	SSL_CTX_free(DefaultClientVerifyContext);
}

static bool PL_Net_OpenSSLInitialize() {
	SSL_library_init();
	OpenSSL_add_all_algorithms();

	DefaultClientContext = SSL_CTX_new(TLS_client_method());
	DefaultClientVerifyContext = SSL_CTX_new(TLS_client_method());

	if (!DefaultClientContext || !DefaultClientVerifyContext) {
		PL_Net_ReportOpenSSLError();

		if (DefaultClientContext)
			SSL_CTX_free(DefaultClientContext);
		if (DefaultClientVerifyContext)
			SSL_CTX_free(DefaultClientVerifyContext);

		return false;
	}

	SSL_CTX_set_verify(DefaultClientVerifyContext, SSL_VERIFY_PEER, nullptr);

#if PLATFORM_WINDOWS
	X509_STORE *store = SSL_CTX_get_cert_store(DefaultClientVerifyContext);
	if (!store) {
		PL_Net_ReportOpenSSLError();
		PL_Net_OpenSSLShutdown();
		return false;
	}

	HCERTSTORE cert_store = CertOpenSystemStoreW(0, L"ROOT");
	if (!cert_store) {
		PL_Net_ReportLastWindowsError();
		PL_Net_OpenSSLShutdown();
		return false;
	}
	Defer{ CertCloseStore(cert_store, 0); };

	PCCERT_CONTEXT cert_context = nullptr;
	while (cert_context = CertEnumCertificatesInStore(cert_store, cert_context)) {
		X509 *x509 = d2i_X509(nullptr, (const unsigned char **)&cert_context->pbCertEncoded, cert_context->cbCertEncoded);

		if (x509) {
			int i = X509_STORE_add_cert(store, x509);
			X509_free(x509);
		}
	}
	CertFreeCertificateContext(cert_context);
#endif

#if PLATFORM_LINUX || PLATFORM_MAC
	long res = SSL_CTX_set_default_verify_paths(DefaultClientVerifyContext);

	if (res != 1) {
		PL_Net_LogOpenSSLError();
		PL_Net_OpenSSLShutdown();
		return NET_E_INIT;
	}
#endif

	return true;
}

static SSL *PL_Net_OpenSSLOpenChannel(SOCKET descriptor, const char *hostname, bool verify) {
	SSL *ssl = SSL_new(verify ? DefaultClientVerifyContext : DefaultClientContext);

	if (!ssl) {
		PL_Net_ReportOpenSSLError();
		return nullptr;
	}

	if (!SSL_set_tlsext_host_name(ssl, hostname)) {
		SSL_free(ssl);
		PL_Net_ReportOpenSSLError();
		return nullptr;
	}

	SSL_set_fd(ssl, (int)descriptor);
	if (SSL_connect(ssl) == -1) {
		SSL_free(ssl);
		PL_Net_ReportOpenSSLError();
		return nullptr;
	}

	return ssl;
}

static void PL_Net_OpenSSLCloseChannel(SSL *ssl) {
	if (ssl) {
		SSL_shutdown(ssl);
		SSL_free(ssl);
	}
}

static int PL_Net_OpenSSLWrite(Net_Socket *net, void *buffer, int length) {
	int written = SSL_write(net->ssl, buffer, length);

	if (written > 0)
		return written;

	if (SSL_get_error(net->ssl, written) == SSL_ERROR_WANT_CONNECT) {
		if (Net_Reconnect(net)) {
			written = SSL_write(net->ssl, buffer, length);
			return written;
		}
	}

	PL_Net_ReportOpenSSLError();
	return written;
}

static int PL_Net_OpenSSLRead(Net_Socket *net, void *buffer, int length) {
	int read = SSL_read(net->ssl, buffer, length);
	return read;
}
#endif

//
//
//

static bool Net_Reconnect(Net_Socket *net) {
	PL_Net_CloseSocketDescriptor(net->descriptor);

	net->descriptor = PL_Net_OpenSocketDescriptor(net->node, net->service, net->type);
	if (net->descriptor == INVALID_SOCKET)
		return false;

	if (net->ssl) {
		SSL_set_fd(net->ssl, (int)net->descriptor);
		if (SSL_connect(net->ssl) == -1) {
			PL_Net_ReportOpenSSLError();
			return false;
		}
	}

	return true;
}

//
//
//

bool Net_Initialize() {
	if (!PL_Net_Initialize())
		return false;

	return PL_Net_OpenSSLInitialize();
}

void Net_Shutdown() {
	PL_Net_OpenSSLShutdown();
	PL_Net_Shutdown();
}

//
//
//

static size_t Net_SocketAllocationSize(const String node, const String service) {
	return sizeof(Net_Socket) + node.length + service.length + 2;
}

Net_Socket *Net_OpenConnection(const String node, const String service, Net_Socket_Type type, Memory_Allocator allocator) {
	SOCKET descriptor = PL_Net_OpenSocketDescriptor(node, service, type);
	if (descriptor == INVALID_SOCKET)
		return nullptr;

	Net_Socket *net = (Net_Socket *)MemoryAllocate(Net_SocketAllocationSize(node, service), allocator);

	if (net) {
		memset(net, 0, sizeof(*net));

		uint8_t *mem        = (uint8_t *)(net + 1);

		net->write          = PL_Net_Write;
		net->read           = PL_Net_Read;
		net->descriptor     = descriptor;
		net->type           = type;
		net->allocator      = allocator;
		net->node.data      = mem;
		net->service.data   = mem + node.length + 1;
		net->node.length    = node.length;
		net->service.length = service.length;

		memcpy(net->node.data, node.data, node.length);
		memcpy(net->service.data, service.data, service.length);

		return net;
	}

	PL_Net_CloseSocketDescriptor(descriptor);

	return nullptr;
}

bool Net_OpenSecureChannel(Net_Socket *net, bool verify) {
	net->ssl = PL_Net_OpenSSLOpenChannel(net->descriptor, (char *)net->node.data, verify);
	if (net->ssl) {
		net->write = PL_Net_OpenSSLWrite;
		net->read  = PL_Net_OpenSSLRead;
		return true;
	}
	return false;
}

void Net_CloseConnection(Net_Socket *net) {
	PL_Net_OpenSSLCloseChannel(net->ssl);
	PL_Net_CloseSocketDescriptor(net->descriptor);

	MemoryFree(net, Net_SocketAllocationSize(net->node, net->service), net->allocator);
	net->descriptor = INVALID_SOCKET;
}

const String Net_GetHostname(Net_Socket *net) {
	return net->node;
}

int Net_Write(Net_Socket *net, void *buffer, int length) {
	return net->write(net, buffer, length);
}

int Net_Read(Net_Socket *net, void *buffer, int length) {
	return net->read(net, buffer, length);
}
