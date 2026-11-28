#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define unlikely(x) (x)
#define likely(x) (x)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define MBEDTLS_PRIVATE(x) x
#define MBEDTLS_ERR_X509_FATAL_ERROR -1
#define MBEDTLS_ERR_X509_ALLOC_FAILED -2
#define MBEDTLS_ASN1_CONSTRUCTED 32
#define MBEDTLS_ASN1_SEQUENCE 16
typedef struct {int tag;size_t len;unsigned char* p;} buffer;
typedef struct node {buffer oid,val;int next_merged;struct node* next;} mbedtls_asn1_named_data;
typedef struct {buffer issuer_raw,subject_raw;mbedtls_asn1_named_data issuer,subject;int ca_istrue,version,pk;} mbedtls_x509_crt;
typedef const uint8_t* cert_t;
static const uint8_t bytes[]={42};
static const uint8_t* s_crt_bundle=bytes;
static unsigned attempts,fail_at,live;
static int key_result;
static void* allocated[16];
static void* mbedtls_calloc(size_t n,size_t size) {
 if(++attempts==fail_at)return NULL;
 // An interior pointer also prevents a raw libc free from appearing valid.
 unsigned char* p=calloc(1,n*size+32);assert(p);p+=16;
 for(unsigned i=0;i<16;++i)if(!allocated[i]){allocated[i]=p;++live;return p;}
 abort();
}
static void mbedtls_free(void* p) {
 if(!p)return;
 for(unsigned i=0;i<16;++i)if(allocated[i]==p){allocated[i]=NULL;--live;free((unsigned char*)p-16);return;}
 assert(!"TLS attempted to free a libc or already released allocation");
}
static void mbedtls_x509_crt_init(mbedtls_x509_crt* c){memset(c,0,sizeof(*c));}
static void mbedtls_x509_crt_free(mbedtls_x509_crt* c){
 if(!c)return;mbedtls_asn1_named_data* n=c->subject.next;
 while(n){mbedtls_asn1_named_data* next=n->next;mbedtls_free(n);n=next;}
 c->subject.next=NULL;
}
static void mbedtls_pk_init(int* p){*p=0;}
static int mbedtls_pk_parse_public_key(int* p,const uint8_t* data,size_t n){return key_result;}
static cert_t esp_crt_find_cert(const unsigned char* issuer,size_t n){return n?bytes:NULL;}
static const uint8_t* esp_crt_get_name(cert_t c){return bytes;}
static uint16_t esp_crt_get_name_len(cert_t c){return 1;}
static const uint8_t* esp_crt_get_key(cert_t c){return bytes;}
static uint16_t esp_crt_get_key_len(cert_t c){return 1;}

/* IDF_CALLBACK */

int main(void){
 mbedtls_x509_crt child={0};child.issuer_raw.p=(unsigned char*)bytes;child.issuer_raw.len=1;
 mbedtls_asn1_named_data names[2]={0};child.issuer.next=&names[0];names[0].next=&names[1];
 // Success, certificate allocation failure, each subject-node failure, then
 // key parsing failure. Every path must use one allocator and release all nodes.
 for(unsigned scenario=0;scenario<5;++scenario){
  attempts=0;fail_at=scenario<4?scenario:0;key_result=scenario==4?-9:0;
  mbedtls_x509_crt* candidate=NULL;
  int result=esp_crt_ca_cb_callback(NULL,&child,&candidate);
  if(!scenario){assert(!result && candidate && live==3);mbedtls_x509_crt_free(candidate);mbedtls_free(candidate);}
  else assert(result<0 && !candidate);
  assert(!live);
 }
 child.issuer_raw.len=0;mbedtls_x509_crt* candidate=NULL;
 assert(!esp_crt_ca_cb_callback(NULL,&child,&candidate) && !candidate && !live);
}
