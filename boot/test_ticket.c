#include <wolfssl/wolfcrypt/user_settings.h>
#include <wolfssl/ssl.h>

#ifndef HAVE_SESSION_TICKET  
#error "HAVE_SESSION_TICKET not defined!"
#endif

#ifdef NO_WOLFSSL_CLIENT
#error "NO_WOLFSSL_CLIENT is defined!"
#endif

int main() {
    WOLFSSL* ssl = NULL;
    wolfSSL_UseSessionTicket(ssl);
    return 0;
}
