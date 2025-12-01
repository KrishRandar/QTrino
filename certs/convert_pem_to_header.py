#!/usr/bin/env python3
"""
Convert PEM certificates to C header files with byte arrays
"""

import sys
import os

def pem_to_der(pem_file):
    """Read PEM file and extract DER data (base64 decoded)"""
    import base64
    
    with open(pem_file, 'r') as f:
        lines = f.readlines()
    
    # Find BEGIN and END markers
    in_cert = False
    b64_data = []
    
    for line in lines:
        line = line.strip()
        if line.startswith('-----BEGIN'):
            in_cert = True
            continue
        elif line.startswith('-----END'):
            break
        elif in_cert:
            b64_data.append(line)
    
    # Decode base64 to get DER
    b64_string = ''.join(b64_data)
    der_data = base64.b64decode(b64_string)
    
    return der_data

def create_c_header(name, der_data, output_file):
    """Create C header file with byte array"""
    
    guard_name = os.path.basename(output_file).replace('.', '_').upper()
    array_name = name.lower().replace('-', '_')
    
    with open(output_file, 'w') as f:
        f.write(f"// Auto-generated certificate header\n")
        f.write(f"#ifndef {guard_name}\n")
        f.write(f"#define {guard_name}\n\n")
        
        # Write array declaration
        f.write(f"static const unsigned char {array_name}_der[] = {{\n")
        
        # Write bytes in rows of 12
        for i in range(0, len(der_data), 12):
            chunk = der_data[i:i+12]
            hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
            f.write(f"    {hex_bytes},\n")
        
        f.write(f"}};\n\n")
        f.write(f"static const int {array_name}_der_len = {len(der_data)};\n\n")
        f.write(f"#endif // {guard_name}\n")
    
    print(f"Created {output_file} ({len(der_data)} bytes)")

def main():
    # Convert CA certificate
    ca_der = pem_to_der('ca_cert.pem')
    create_c_header('ca_cert', ca_der, 'ca_cert.h')
    
    # Convert server certificate
    server_cert_der = pem_to_der('server_cert.pem')
    create_c_header('server_cert', server_cert_der, 'server_cert.h')
    
    # Convert server key
    server_key_der = pem_to_der('server_key.pem')
    create_c_header('server_key', server_key_der, 'server_key.h')
    
    # Convert client certificate
    client_cert_der = pem_to_der('client_cert.pem')
    create_c_header('client_cert', client_cert_der, 'client_cert.h')
    
    # Convert client key
    client_key_der = pem_to_der('client_key.pem')
    create_c_header('client_key', client_key_der, 'client_key.h')
    
    print("\nAll certificates converted successfully!")

if __name__ == '__main__':
    main()
