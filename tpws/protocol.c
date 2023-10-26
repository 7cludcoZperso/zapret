#define _GNU_SOURCE

#include "protocol.h"
#include "helpers.h"
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <string.h>


const char *http_methods[] = { "GET /","POST /","HEAD /","OPTIONS /","PUT /","DELETE /","CONNECT /","TRACE /",NULL };
bool IsHttp(const uint8_t *data, size_t len)
{
	const char **method;
	size_t method_len;
	for (method = http_methods; *method; method++)
	{
		method_len = strlen(*method);
		if (method_len <= len && !memcmp(data, *method, method_len))
			return true;
	}
	return false;
}
bool IsHttpReply(const uint8_t *data, size_t len)
{
	// HTTP/1.x 200\r\n
	return len>14 && !memcmp(data,"HTTP/1.",7) && (data[7]=='0' || data[7]=='1') && data[8]==' ' &&
		data[9]>='0' && data[9]<='9' &&
		data[10]>='0' && data[10]<='9' &&
		data[11]>='0' && data[11]<='9';
}
int HttpReplyCode(const uint8_t *data, size_t len)
{
	return (data[9]-'0')*100 + (data[10]-'0')*10 + (data[11]-'0');
}
bool HttpExtractHeader(const uint8_t *data, size_t len, const char *header, char *buf, size_t len_buf)
{
	const uint8_t *p, *s, *e = data + len;

	p = (uint8_t*)strncasestr((char*)data, header, len);
	if (!p) return false;
	p += strlen(header);
	while (p < e && (*p == ' ' || *p == '\t')) p++;
	s = p;
	while (s < e && (*s != '\r' && *s != '\n' && *s != ' ' && *s != '\t')) s++;
	if (s > p)
	{
		size_t slen = s - p;
		if (buf && len_buf)
		{
			if (slen >= len_buf) slen = len_buf - 1;
			for (size_t i = 0; i < slen; i++) buf[i] = tolower(p[i]);
			buf[slen] = 0;
		}
		return true;
	}
	return false;
}
bool HttpExtractHost(const uint8_t *data, size_t len, char *host, size_t len_host)
{
	return HttpExtractHeader(data, len, "\nHost:", host, len_host);
}
const char *HttpFind2ndLevelDomain(const char *host)
{
	const char *p=NULL;
	if (*host)
	{
		for (p = host + strlen(host)-1; p>host && *p!='.'; p--);
		if (*p=='.') for (p--; p>host && *p!='.'; p--);
		if (*p=='.') p++;
	}
	return p;
}
// DPI redirects are global redirects to another domain
bool HttpReplyLooksLikeDPIRedirect(const uint8_t *data, size_t len, const char *host)
{
	char loc[256],*redirect_host, *p;
	int code;
	
	if (!host || !*host) return false;
	
	code = HttpReplyCode(data,len);
	
	if (code!=302 && code!=307 || !HttpExtractHeader(data,len,"\nLocation:",loc,sizeof(loc))) return false;

	// something like : https://censor.net/badpage.php?reason=denied&source=RKN
		
	if (!strncmp(loc,"http://",7))
		redirect_host=loc+7;
	else if (!strncmp(loc,"https://",8))
		redirect_host=loc+8;
	else
		return false;
		
	// somethinkg like : censor.net/badpage.php?reason=denied&source=RKN
	
	for(p=redirect_host; *p && *p!='/' ; p++);
	*p=0;
	if (!*redirect_host) return false;

	// somethinkg like : censor.net
	
	// extract 2nd level domains

	const char *dhost = HttpFind2ndLevelDomain(host);
	const char *drhost = HttpFind2ndLevelDomain(redirect_host);
	
	return strcasecmp(dhost, drhost)!=0;
}



bool IsTLSClientHello(const uint8_t *data, size_t len)
{
	return len>=6 && data[0]==0x16 && data[1]==0x03 && data[2]>=0x01 && data[2]<=0x03 && data[5]==0x01 && (pntoh16(data+3)+5)<=len;
}
bool TLSFindExt(const uint8_t *data, size_t len, uint16_t type, const uint8_t **ext, size_t *len_ext)
{
	// +0
	// u8	ContentType: Handshake
	// u16	Version: TLS1.0
	// u16	Length
	// +5 
	// u8	HandshakeType: ClientHello
	// u24	Length
	// u16	Version
	// c[32] random
	// u8	SessionIDLength
	//	<SessionID>
	// u16	CipherSuitesLength
	//	<CipherSuites>
	// u8	CompressionMethodsLength
	//	<CompressionMethods>
	// u16	ExtensionsLength

	size_t l,ll;

	l = 1+2+2+1+3+2+32;
	// SessionIDLength
	if (len<(l+1)) return false;
	ll = data[6]<<16 | data[7]<<8 | data[8]; // HandshakeProtocol length
	if (len<(ll+9)) return false;
	l += data[l]+1;
	// CipherSuitesLength
	if (len<(l+2)) return false;
	l += pntoh16(data+l)+2;
	// CompressionMethodsLength
	if (len<(l+1)) return false;
	l += data[l]+1;
	// ExtensionsLength
	if (len<(l+2)) return false;

	data+=l; len-=l;
	l=pntoh16(data);
	data+=2; len-=2;
	if (len<l) return false;

	while(l>=4)
	{
		uint16_t etype=pntoh16(data);
		size_t elen=pntoh16(data+2);
		data+=4; l-=4;
		if (l<elen) break;
		if (etype==type)
		{
			if (ext && len_ext)
			{
				*ext = data;
				*len_ext = elen;
			}
			return true;
		}
		data+=elen; l-=elen;
	}

	return false;
}
bool TLSHelloExtractHost(const uint8_t *data, size_t len, char *host, size_t len_host)
{
	const uint8_t *ext;
	size_t elen;

	if (!TLSFindExt(data,len,0,&ext,&elen)) return false;
	// u16	data+0 - name list length
	// u8	data+2 - server name type. 0=host_name
	// u16	data+3 - server name length
	if (elen<5 || ext[2]!=0) return false;
	size_t slen = pntoh16(ext+3);
	ext+=5; elen-=5;
	if (slen<elen) return false;
	if (ext && len_host)
	{
		if (slen>=len_host) slen=len_host-1;
		for(size_t i=0;i<slen;i++) host[i]=tolower(ext[i]);
		host[slen]=0;
	}
	return true;
}
