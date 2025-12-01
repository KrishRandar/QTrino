/*
 * gen_pqc_certs.c - Generate PQC Certificate Chain
 * 
 * Generates:
 * 1. Root CA (self-signed, ML-DSA-44)
 * 2. Server certificate (signed by CA, ML-DSA-44)
 * 3. Client certificate (signed by CA, ML-DSA-44)
 * 
 * All certificates and keys are saved as DER files and converted to C headers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/dilithium.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/asn.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssl/wolfcrypt/random.h>

#define CERT_BUF_SIZE 4096
#define KEY_BUF_SIZE  8192

// Helper function to write DER file
static int write_der_file(const char* filename, const unsigned char* data, int len) {
    FILE* f = fopen(filename, "wb");
    if (!f) {
        printf("ERROR: Cannot open %s for writing\n", filename);
        return -1;
    }
    fwrite(data, 1, len, f);
    fclose(f);
    printf("[OK] Wrote %s (%d bytes)\n", filename, len);
    return 0;
}

// Helper function to convert DER to C header
static int der_to_header(const char* der_file, const char* header_file, const char* var_name) {
    FILE* fin = fopen(der_file, "rb");
    if (!fin) {
        printf("ERROR: Cannot open %s\n", der_file);
        return -1;
    }
    
    // Get file size
    fseek(fin, 0, SEEK_END);
    long size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    
    // Read data
    unsigned char* data = malloc(size);
    fread(data, 1, size, fin);
    fclose(fin);
    
    // Write header
    FILE* fout = fopen(header_file, "w");
    if (!fout) {
        printf("ERROR: Cannot open %s for writing\n", header_file);
        free(data);
        return -1;
    }
    
    fprintf(fout, "// Auto-generated from %s\n", der_file);
    fprintf(fout, "unsigned char %s[] = {\n  ", var_name);
    
    for (long i = 0; i < size; i++) {
        fprintf(fout, "0x%02x", data[i]);
        if (i < size - 1) fprintf(fout, ", ");
        if ((i + 1) % 12 == 0 && i < size - 1) fprintf(fout, "\n  ");
    }
    
    fprintf(fout, "\n};\n");
    fprintf(fout, "unsigned int %s_len = %ld;\n", var_name, size);
    
    fclose(fout);
    free(data);
    
    printf("[OK] Generated %s\n", header_file);
    return 0;
}

int main(void) {
    int ret;
    WC_RNG rng;
    dilithium_key ca_key, server_key, client_key;
    Cert ca_cert, server_cert, client_cert;
    
    unsigned char ca_key_der[KEY_BUF_SIZE];
    unsigned char ca_cert_der[CERT_BUF_SIZE];
    unsigned char server_key_der[KEY_BUF_SIZE];
    unsigned char server_cert_der[CERT_BUF_SIZE];
    unsigned char client_key_der[KEY_BUF_SIZE];
    unsigned char client_cert_der[CERT_BUF_SIZE];
    
    int ca_key_len, ca_cert_len;
    int server_key_len, server_cert_len;
    int client_key_len, client_cert_len;
    
    printf("========================================\n");
    printf("PQC Certificate Generation (ML-DSA-44)\n");
    printf("========================================\n\n");
    
    // Initialize RNG
    ret = wc_InitRng(&rng);
    if (ret != 0) {
        printf("ERROR: RNG init failed: %d\n", ret);
        return 1;
    }
    printf("[OK] RNG initialized\n");
    
    //=========================================================================
    // STEP 1: Generate Root CA
    //=========================================================================
    printf("\n[STEP 1] Generating Root CA...\n");
    
    // Initialize CA key
    ret = wc_dilithium_init(&ca_key);
    if (ret != 0) {
        printf("ERROR: CA key init failed: %d\n", ret);
        return 1;
    }
    
    // Generate CA keypair (ML-DSA-44 = Level 2)
    ret = wc_dilithium_make_key(&ca_key, &rng);
    if (ret != 0) {
        printf("ERROR: CA key generation failed: %d\n", ret);
        return 1;
    }
    printf("[OK] CA keypair generated\n");
    
    // Export CA private key
    ca_key_len = wc_dilithium_export_key(&ca_key, ca_key_der, sizeof(ca_key_der));
    if (ca_key_len < 0) {
        printf("ERROR: CA key export failed: %d\n", ca_key_len);
        return 1;
    }
    write_der_file("certs/ca_key.der", ca_key_der, ca_key_len);
    
    // Create CA certificate (self-signed)
    wc_InitCert(&ca_cert);
    strncpy(ca_cert.subject.country, "US", CTC_NAME_SIZE);
    strncpy(ca_cert.subject.state, "CA", CTC_NAME_SIZE);
    strncpy(ca_cert.subject.org, "QTrino Labs", CTC_NAME_SIZE);
    strncpy(ca_cert.subject.commonName, "Root CA", CTC_NAME_SIZE);
    ca_cert.isCA = 1;
    ca_cert.sigType = CTC_DILITHIUM_LEVEL2;
    
    ca_cert_len = wc_MakeSelfCert(&ca_cert, ca_cert_der, sizeof(ca_cert_der), &ca_key, &rng);
    if (ca_cert_len < 0) {
        printf("ERROR: CA cert generation failed: %d\n", ca_cert_len);
        return 1;
    }
    write_der_file("certs/ca_cert.der", ca_cert_der, ca_cert_len);
    printf("[OK] Root CA certificate created\n");
    
    //=========================================================================
    // STEP 2: Generate Server Certificate
    //=========================================================================
    printf("\n[STEP 2] Generating Server Certificate...\n");
    
    ret = wc_dilithium_init(&server_key);
    if (ret != 0) {
        printf("ERROR: Server key init failed: %d\n", ret);
        return 1;
    }
    
    ret = wc_dilithium_make_key(&server_key, &rng);
    if (ret != 0) {
        printf("ERROR: Server key generation failed: %d\n", ret);
        return 1;
    }
    printf("[OK] Server keypair generated\n");
    
    server_key_len = wc_dilithium_export_key(&server_key, server_key_der, sizeof(server_key_der));
    if (server_key_len < 0) {
        printf("ERROR: Server key export failed: %d\n", server_key_len);
        return 1;
    }
    write_der_file("certs/server_key.der", server_key_der, server_key_len);
    
    // Create server certificate
    wc_InitCert(&server_cert);
    strncpy(server_cert.subject.country, "US", CTC_NAME_SIZE);
    strncpy(server_cert.subject.state, "CA", CTC_NAME_SIZE);
    strncpy(server_cert.subject.org, "QTrino Labs", CTC_NAME_SIZE);
    strncpy(server_cert.subject.commonName, "DTLS Server", CTC_NAME_SIZE);
    server_cert.isCA = 0;
    server_cert.sigType = CTC_DILITHIUM_LEVEL2;
    
    server_cert_len = wc_MakeCert(&server_cert, server_cert_der, sizeof(server_cert_der), 
                                   NULL, &server_key, &rng);
    if (server_cert_len < 0) {
        printf("ERROR: Server cert creation failed: %d\n", server_cert_len);
        return 1;
    }
    
    // Sign server cert with CA
    server_cert_len = wc_SignCert(server_cert.bodySz, server_cert.sigType, 
                                   server_cert_der, sizeof(server_cert_der),
                                   NULL, &ca_key, &rng);
    if (server_cert_len < 0) {
        printf("ERROR: Server cert signing failed: %d\n", server_cert_len);
        return 1;
    }
    write_der_file("certs/server_cert.der", server_cert_der, server_cert_len);
    printf("[OK] Server certificate created and signed\n");
    
    //=========================================================================
    // STEP 3: Generate Client Certificate
    //=========================================================================
    printf("\n[STEP 3] Generating Client Certificate...\n");
    
    ret = wc_dilithium_init(&client_key);
    if (ret != 0) {
        printf("ERROR: Client key init failed: %d\n", ret);
        return 1;
    }
    
    ret = wc_dilithium_make_key(&client_key, &rng);
    if (ret != 0) {
        printf("ERROR: Client key generation failed: %d\n", ret);
        return 1;
    }
    printf("[OK] Client keypair generated\n");
    
    client_key_len = wc_dilithium_export_key(&client_key, client_key_der, sizeof(client_key_der));
    if (client_key_len < 0) {
        printf("ERROR: Client key export failed: %d\n", client_key_len);
        return 1;
    }
    write_der_file("certs/client_key.der", client_key_der, client_key_len);
    
    // Create client certificate
    wc_InitCert(&client_cert);
    strncpy(client_cert.subject.country, "US", CTC_NAME_SIZE);
    strncpy(client_cert.subject.state, "CA", CTC_NAME_SIZE);
    strncpy(client_cert.subject.org, "QTrino Labs", CTC_NAME_SIZE);
    strncpy(client_cert.subject.commonName, "RISC-V Client", CTC_NAME_SIZE);
    client_cert.isCA = 0;
    client_cert.sigType = CTC_DILITHIUM_LEVEL2;
    
    client_cert_len = wc_MakeCert(&client_cert, client_cert_der, sizeof(client_cert_der),
                                   NULL, &client_key, &rng);
    if (client_cert_len < 0) {
        printf("ERROR: Client cert creation failed: %d\n", client_cert_len);
        return 1;
    }
    
    // Sign client cert with CA
    client_cert_len = wc_SignCert(client_cert.bodySz, client_cert.sigType,
                                   client_cert_der, sizeof(client_cert_der),
                                   NULL, &ca_key, &rng);
    if (client_cert_len < 0) {
        printf("ERROR: Client cert signing failed: %d\n", client_cert_len);
        return 1;
    }
    write_der_file("certs/client_cert.der", client_cert_der, client_cert_len);
    printf("[OK] Client certificate created and signed\n");
    
    //=========================================================================
    // STEP 4: Convert to C Headers
    //=========================================================================
    printf("\n[STEP 4] Converting to C headers...\n");
    
    der_to_header("certs/ca_cert.der", "certs/ca_cert.h", "ca_cert_der");
    der_to_header("certs/server_cert.der", "certs/server_cert.h", "server_cert_der");
    der_to_header("certs/server_key.der", "certs/server_key.h", "server_key_der");
    der_to_header("certs/client_cert.der", "certs/client_cert.h", "client_cert_der");
    der_to_header("certs/client_key.der", "certs/client_key.h", "client_key_der");
    
    // Cleanup
    wc_dilithium_free(&ca_key);
    wc_dilithium_free(&server_key);
    wc_dilithium_free(&client_key);
    wc_FreeRng(&rng);
    
    printf("\n========================================\n");
    printf("Certificate Generation Complete!\n");
    printf("========================================\n");
    printf("\nGenerated files:\n");
    printf("  certs/ca_cert.der, certs/ca_cert.h\n");
    printf("  certs/server_cert.der, certs/server_cert.h\n");
    printf("  certs/server_key.der, certs/server_key.h\n");
    printf("  certs/client_cert.der, certs/client_cert.h\n");
    printf("  certs/client_key.der, certs/client_key.h\n");
    
    return 0;
}
