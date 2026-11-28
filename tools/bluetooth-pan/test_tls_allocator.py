#!/usr/bin/env python3
"""Run the actual IDF certificate callback with a strict non-libc allocator."""
import argparse,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--idf',type=Path,required=True);a=p.parse_args()
source=(a.idf/'components/mbedtls/esp_crt_bundle/esp_crt_bundle.c').read_text()
begin=source.index('static int esp_crt_ref_asn1(')
end=source.index('#endif /* CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY */',begin)
fixture=Path(__file__).with_name('tls_allocator_fixture.c').read_text()
with tempfile.TemporaryDirectory(prefix='tls-allocator-') as tmp:
    c=Path(tmp)/'callback.c';exe=Path(tmp)/'callback'
    c.write_text(fixture.replace('/* IDF_CALLBACK */',source[begin:end]))
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-g','-fsanitize=undefined',
                    '-fno-sanitize-recover=all',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
print('IDF certificate callback allocator ownership and failure cleanup passed')
