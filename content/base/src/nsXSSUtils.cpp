/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTArray.h"
#include "nsIURI.h"
#include "nsAString.h"
#include "nsXSSUtils.h"
#include "nsIIOService.h"
#include "nsIURL.h"
#include "nsCOMPtr.h"
#include "nsIIOService.h"
#include "nsNetUtil.h"
#include "nsEscape.h"
#include "nsIEffectiveTLDService.h"
#include <string.h>
#include <wchar.h>
#include "math.h"
#include "nsGenericHTMLElement.h"
#include "mozAutoDocUpdate.h"
#include "nsIDocument.h"
#include "mozilla/dom/Element.h"
#include "mozilla/ErrorResult.h"
#include <algorithm>

#include "nsScriptLoader.h"
#include "nsHtml5Module.h"

using namespace mozilla::dom;

#ifdef PR_LOGGING
static PRLogModuleInfo *gXssPRLog;
#endif


#define THRESHOLD 0.2

#define MIN_INL_PARAM_LEN 10
#define MIN_EXT_PARAM_LEN 5
#define MIN_INL_MATCH_LEN 10
#define MIN_EXT_MATCH_LEN 5


#define PATH_CHARS " -/.;"
#define QUERY_CHARS " &=-"
#define REF_CHARS "-"
#define MATCH_CHARS "-" // includes alphanumeric, so it is really [\w-]

Parameter::Parameter(const nsAString& aName, const nsAString& aValue,
                     bool aDangerous, bool aSpecial, bool aAttack)
  : name(aName),
    value(aValue),
    attack(aAttack),
    dangerous(aDangerous),
    special(aSpecial)
{ }

Parameter::Parameter(const char* aName, const char* aValue,
                     bool aDangerous, bool aSpecial, bool aAttack)
  : name(NS_ConvertASCIItoUTF16(aName)),
    value(NS_ConvertASCIItoUTF16(aValue)),
    attack(aAttack),
    dangerous(aDangerous),
    special(aSpecial)
{ }

bool
Parameter::operator==(const Parameter& other) const
{
  return name.Equals(other.name) && value.Equals(other.value) &&
    attack == other.attack && dangerous == other.dangerous &&
    special == other.special;
}

/* for debugging */
void
Parameter::print() const
{
  printf("name = '%s', ", NS_ConvertUTF16toUTF8(this->name).get());
  printf("value = '%s', ", NS_ConvertUTF16toUTF8(this->value).get());
  if (attack) {
    printf(" attack, ");
  }
  if (dangerous) {
    printf(" dangerous, ");
  }
  if (special) {
    printf(" special");
  }
  printf("\n");
}

/* [a-zA-Z0-9_] */
bool
IsAlphanumeric(PRUnichar c)
{
  return (PRUnichar('a') <= c && c <= PRUnichar('z')) ||
    (PRUnichar('A') <= c && c <= PRUnichar('Z')) ||
    (PRUnichar('0') <= c && c <= PRUnichar('9')) ||
    c == PRUnichar('_');
}

bool
IsCharListed(PRUnichar c, const char *chars)
{
  for (PRUint8 i = 0; i < strlen(chars); i++)
    if (c == PRUnichar(chars[i])) {
      return true;
    }
  return false;
}

nsresult
nsXSSUtils::TrimSafeChars(const nsAString& str, nsAString& result,
                          const char *chars, PRUint8 flags)
{
  if (str.IsEmpty()) {
    result = str;
    return NS_OK;
  }
  PRUint32 start;
  for (start = 0; start < str.Length(); start++) {
    PRUnichar cur = str[start];
    if (!IsAlphanumeric(cur) && !IsCharListed(cur, chars)) {
      break;
    }
  }
  PRUint32 end;
  for (end = str.Length() - 1; end > start; end--) {
    PRUnichar cur = str[end];
    if (!IsAlphanumeric(cur) && !IsCharListed(cur, chars)) {
      break;
    }
  }
  if (start > end) {
    result.Truncate(0);
    return NS_OK;
  }
  bool leave_beg = (flags >> 0) % 2;
  bool leave_end = (flags >> 1) % 2;
  if (leave_beg) {
    start = 0;
  }
  if (leave_end) {
    end = str.Length();
  }

  result = Substring(str, start, end - start + 1);
  return NS_OK;
}

nsresult
nsXSSUtils::ParseURI(nsIURI *aURI, ParameterArray& aParams, nsIDocument* doc)
{
  nsresult rv;
  nsAutoString result;
  nsCOMPtr<nsIURL> url = do_QueryInterface(aURI, &rv);
  if (NS_FAILED(rv)) {
    LOG_XSS("Not an nsIURL");
    return rv;
  }

  nsAutoCString path8, query8, ref8;
  url->GetFilePath(path8);
  url->GetQuery(query8);
  url->GetRef(ref8);

  nsAutoString path, query, ref;
  nsAutoString tPath, tQuery, tRef;
  CopyUTF8toUTF16(path8, path);
  CopyUTF8toUTF16(query8, query);
  CopyUTF8toUTF16(ref8, ref);

  //trimming was originally used to cut away parts of the request that
  //did not contain suspicious characters, but it has now been
  //replaced by a more conservative policy, and the function is just
  //used as if it was something like "hasSuspiciousChars".
  nsXSSUtils::UnescapeLoop(path, path, doc);
  nsXSSUtils::TrimSafeChars(path, tPath, PATH_CHARS);
  nsXSSUtils::UnescapeLoop(query, query, doc);
  nsXSSUtils::TrimSafeChars(query, tQuery, QUERY_CHARS);
  nsXSSUtils::UnescapeLoop(ref, ref, doc);
  nsXSSUtils::TrimSafeChars(ref, tRef, REF_CHARS);

  if (!tPath.IsEmpty() || !tQuery.IsEmpty() || !tRef.IsEmpty()) {
    result += Substring(path, 1, path.Length() - 1); // trim '/'
    if (!result.IsEmpty() && !query.IsEmpty()) {
      result += NS_LITERAL_STRING("?");
    }
    result += query;
    if (!result.IsEmpty() && !ref.IsEmpty()) {
      result += NS_LITERAL_STRING("#");
    }
    result += ref;
    aParams.AppendElement(Parameter(NS_LITERAL_STRING("URL"),
                                    result, true, true));
  }
  return NS_OK;
}

nsresult
nsXSSUtils::ParsePOST(char *request, ParameterArray& aParams, nsIDocument* doc)
{
  char* separator = strstr(request, "\r\n\r\n");
  char* urlencoded = strstr(request, "application/x-www-form-urlencoded");
  char* multipart = strstr(request, "multipart/form-data");

  if (urlencoded && urlencoded < separator) {
    nsAutoString params;
    params.Assign(NS_ConvertASCIItoUTF16(separator+4));

    LOG_XSS_1("POST payload: %s", NS_ConvertUTF16toUTF8(params).get());

    // replace + with ' '
    PRUnichar* cur = params.BeginWriting();
    PRUnichar* end = params.EndWriting();
    for (; cur < end; ++cur) {
      if (PRUnichar('+') == *cur) {
        *cur = PRUnichar(' ');
      }
    }
    // escape it like a normal url path
    nsXSSUtils::UnescapeLoop(params, params, doc);
    nsXSSUtils::TrimSafeChars(params, params, QUERY_CHARS);
    LOG_XSS_1("POST escaped payload: %s",
              NS_ConvertUTF16toUTF8(params).get());
    if (!params.IsEmpty()) {
      aParams.AppendElement(Parameter(NS_LITERAL_STRING("POST"),
                                      params, true, true));
    }
  } else if(multipart && multipart < separator) {
    // TODO: this works, but it's not optimal: it would be best to
    // isolate the single parameters (and concatenate them in the
    // paramarray). this wastes at lot of cpu power during the string
    // matching to attempt matching data that is not under the control
    // of the attacker (MIME boundaries)
    nsAutoString params;
    params.Assign(NS_ConvertASCIItoUTF16(separator+4));

    LOG_XSS_1("MIME POST payload: %s",
              NS_ConvertUTF16toUTF8(params).get());
    if (!params.IsEmpty()) {
      aParams.AppendElement(Parameter(NS_LITERAL_STRING("MIMEPOST"),
                                          params, true, true));
    }
  } else {
    LOG_XSS("Unsupported or invalid POST request");
  }
  return NS_OK;

}

nsresult
nsXSSUtils::UnescapeLoop(const nsAString& aString, nsAString& result,
                         nsIDocument *doc)
{
  nsAutoString tmp;
  nsAutoCString tmp8;

  tmp = aString;
  do {
    result = tmp;
    if (doc) {
      nsXSSUtils::DecodeHTMLEntities(tmp, tmp, doc);
    }
    // expensive?
    CopyUTF16toUTF8(tmp, tmp8);
    NS_UnescapeURL(tmp8);
    CopyUTF8toUTF16(tmp8, tmp);
  } while(!tmp.Equals(result));

  return NS_OK;
}

nsresult
nsXSSUtils::DecodeHTMLEntities(const nsAString& aString, nsAString& result,
                               nsIDocument* doc)
{
  // 2 bugs so far, both in svg reftest. one seems to be a random
  // failure, while the second seems to be triggered by the call to
  // setinnerhtml. specifically, an assert condition is violated.

  LOG_XSS("DecodeHTMLEntities");
  result = aString;

  Element *rootElement = doc->GetRootElement();
  if (rootElement) {
    LOG_XSS("Has root element.");
    if (rootElement->GetNameSpaceID() == kNameSpaceID_SVG) {
      LOG_XSS("SVG Doc, bailing out");
      return NS_OK; // SVG doesn't support setting a title
    }
  } else {
    LOG_XSS("No root element");
  }

  mozAutoDocUpdate updateBatch(doc, UPDATE_CONTENT_MODEL, true);

  nsCOMPtr<nsINodeInfo> titleInfo;
  titleInfo =
    doc->NodeInfoManager()->GetNodeInfo(nsGkAtoms::title, nullptr,
                                        kNameSpaceID_XHTML,
                                        nsIDOMNode::ELEMENT_NODE);
  if (!titleInfo) {
    return NS_OK;
  }
  nsIContent* title = NS_NewHTMLTitleElement(titleInfo.forget());
  if (!title) {
    return NS_OK;
  }
  mozilla::ErrorResult rv;
  nsCOMPtr<nsGenericHTMLElement> elTitle(do_QueryInterface(title));
  elTitle->SetInnerHTML(aString, rv);
  NS_ENSURE_SUCCESS(rv.ErrorCode(), rv.ErrorCode());
  title->GetTextContent(result);
  return NS_OK;
}

PRUint32
nsXSSUtils::GetHostLimit(nsIURI* aURI)
{
  nsAutoCString prePath, path;
  aURI->GetPrePath(prePath);
  aURI->GetPath(path);
  if (!path.IsEmpty()) {
    return prePath.Length() + 1;
  }
  NS_ERROR("Path Empty. Does this ever happen? It shouldn't");
  return prePath.Length();
}


nsresult
nsXSSUtils::GetDomain(nsIURI* aURI, nsAString& result)
{
  nsresult rv;
  nsCOMPtr<nsIEffectiveTLDService>
    eTLDService(do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);
  nsAutoCString result8;
  rv = eTLDService->GetBaseDomain(aURI, 0, result8);
  // we might have a problem with unicode domains, as this
  // returns an ASCII string. If the conversion is lossy there is a
  // chance for a clash between different names.
  if (rv == NS_ERROR_HOST_IS_IP_ADDRESS) {
    aURI->GetHost(result8);
    result = NS_ConvertUTF8toUTF16(result8);
    return NS_OK;
  }
  NS_ENSURE_SUCCESS(rv, rv);
  result = NS_ConvertUTF8toUTF16(result8);
  return NS_OK;
}

void
nsXSSUtils::InitializeStatics()
{
#ifdef PR_LOGGING
  gXssPRLog = PR_NewLogModule("XSS");
#endif
  LOG_XSS("Initialized Statics for XSS Utils");
}

bool
nsXSSUtils::FindInlineXSS(const nsAString& aParam, const nsAString& aScript)
{
  MatchRes mres;
    // the script could be cut for efficiency, but since there is a
    // bound on the parameter length (which is usually a small number), it
    // probably does not improve performance much.
  if (aParam.Length() >= aScript.Length()) {
    // base case where the attacker injects the whole script. Since
    // the tags/quotes are stripped by the parser, the script ends up
    // being shorter.
    nsXSSUtils::P1FastMatchReverse(aParam, aScript, THRESHOLD, mres);
  } else if (aParam.Length() >= aScript.Length() * (1-THRESHOLD)) {
    // if we do not care about partial injections the only possible
    // case of |script| > |param| is due to sanitization increasing
    // the size of the script (e.g. add slashes). In this case, it
    // makes sense to match only if the length is close enough.
    nsXSSUtils::P1FastMatch(aParam, aScript, THRESHOLD, mres);
  }
  mres.ClearInvalidMatches(THRESHOLD, true);

  for (PRUint32 i = 0; i < mres.elem_.Length(); i++) {
    PRUint32 beg = mres[i].matchBeg_, end = mres[i].matchEnd_;
    const nsAString &match = Substring(aScript, beg, end-beg+1);
    nsAutoString tMatch;
    nsXSSUtils::TrimSafeChars(match, tMatch, MATCH_CHARS);
    if (tMatch.IsEmpty())
      continue; // does not contain interesting chars

    LOG_XSS_2("Examining Match in FindInlineXSS: %d %d\n", beg, end);
    LOG_XSS_1("Matched string: %s\n", NS_ConvertUTF16toUTF8(match).get());
    // check: tainted string must not be at the beginning of the script
    if (beg == 0) {
      LOG_XSS("Match is at the beginning of script!");
      return true;
    }
  }
  return false;
}

bool
nsXSSUtils::FindExternalXSS(const nsAString& aParam, nsIURI *aURI)
{
  MatchRes mres;
  nsAutoString url;
  // the string has already been converted up in the call stack. if
  // this is an expensive operation, i guess it could be passed
  // directly.
  nsAutoCString url8;
  aURI->GetSpec(url8);
  CopyUTF8toUTF16(url8, url);
  // same logic wrt to length as findinlinexss
  if (aParam.Length() >= url.Length()) {
    nsXSSUtils::P1FastMatchReverse(aParam, url, THRESHOLD, mres);
  } else if (aParam.Length() >= url.Length() * (1-THRESHOLD)) {
    nsXSSUtils::P1FastMatch(aParam, url, THRESHOLD, mres);
  }
  mres.ClearInvalidMatches(THRESHOLD, false);
  PRUint32 safeIndex = GetHostLimit(aURI);
  for (PRUint32 i = 0; i < mres.Length(); i++) {
    LOG_XSS_2("Match in FindExternalXSS: %d %d\n", mres[i].matchBeg_,
              mres[i].matchEnd_);
    if (mres[i].matchBeg_  < safeIndex) {
      LOG_XSS("Match controls host portion");
      return true;
    }
  }
  return false;
}

nsresult
nsXSSUtils::CheckInline(const nsAString& aScript, ParameterArray& aParams)
{
  if (aParams.IsEmpty()) {
    return NS_OK;
  }
  for (PRUint32 i = 0; i < aParams.Length(); i++) {
    if (!aParams[i].dangerous) {
      continue;
    }
    if (aParams[i].value.Length() < MIN_INL_PARAM_LEN) {
      continue;
    }
    aParams[i].attack = nsXSSUtils::FindInlineXSS(aParams[i].value, aScript);
  }
  return NS_OK;
}

nsresult
nsXSSUtils::CheckExternal(nsIURI *aURI, nsIURI *aPageURI,
                          ParameterArray& aParams)
{
  if (aParams.IsEmpty()) {
    return NS_OK;
  }
  nsAutoString domain, pageDomain;
  nsAutoCString spec;
  nsXSSUtils::GetDomain(aURI, domain);
  nsXSSUtils::GetDomain(aPageURI, pageDomain);
  if (domain.Equals(pageDomain)) {
    return NS_OK;
  }
  for (PRUint32 i = 0; i < aParams.Length(); i++) {
    if (!aParams[i].dangerous) {
      continue;
    }
    if (aParams[i].value.Length() < MIN_EXT_PARAM_LEN) {
      continue;
    }
    // TODO: utf8.Lenth() returns bytes, not code points. the length
    // might be greater than the same str in utf16
    aURI->GetSpec(spec);
    aParams[i].attack = nsXSSUtils::FindExternalXSS(aParams[i].value, aURI);
  }
  return NS_OK;
}

bool
nsXSSUtils::HasAttack(ParameterArray& aParams)
{
  for (PRUint32 i = 0; i < aParams.Length(); i++)
    if (aParams[i].attack) {
      return true;
    }
  return false;
}

/*****************
 * p1Match stuff
 ****************/

void
MatchRes::ClearInvalidMatches(double threshold, bool isInline = false)
{
  PRUint32 minLen;
  if (isInline)
    minLen = MIN_INL_MATCH_LEN;
  else
    minLen = MIN_EXT_MATCH_LEN;
  for (PRUint32 i = 0; i < elem_.Length(); i++) {
    if (elem_[i].dist_ > costThresh_ ||
        (elem_[i].matchEnd_ - elem_[i].matchBeg_ < minLen)) {
      elem_.RemoveElementAt(i);
      i--;
    }
  }
}


#define DEFAULT_COST 20

#define IS_ASCII(u)       ((u) < 0x80)


// We map x -> x, except for upper-case letters,
 // which we map to their lower-case equivalents.
const PRUint8 gASCIIToLower [128] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
  0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
  0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
  0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
  0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
};

// this version does not put non-ascii chars to lowercase
MOZ_ALWAYS_INLINE PRUnichar
ASCIIToLowerCase(PRUnichar aChar)
{
  if (IS_ASCII(aChar)) {
    return gASCIIToLower[aChar];
  }
  return aChar;
}

PRUint8
DistMetric::GetICost(PRUnichar sc)
{
  return DEFAULT_COST;
}

PRUint8
DistMetric::GetDCost(PRUnichar pc)
{
  return DEFAULT_COST;
}

PRUint8
DistMetric::GetSCost(PRUnichar pc, PRUnichar sc)
{
  // in this case, normalization might not be very useful...
  if (ASCIIToLowerCase(pc) == ASCIIToLowerCase(sc)) {
    return 0;
  }
  return ( (GetICost(sc) + GetDCost(pc)) * 3) / 4;
}

PRUnichar
DistMetric::GetNorm(PRUnichar c)
{
  return ASCIIToLowerCase(c);
}

const PRUnichar DistVector::NOT_ASCII = PRUnichar(~0x007F);

bool
DistVector::IsASCII(PRUnichar c)
{
  return !(c & DistVector::NOT_ASCII);
}

PRUint8
DistVector::GetASCII(PRUnichar c)
{
  return c;
}

DistVector::DistVector()
{
  mMap.Init();
  memset(mArray, 0, sizeof(PRInt32)*128);
}

PRInt32
DistVector::GetDiff(PRUnichar c)
{
  if (IsASCII(c)) {
    return mArray[GetASCII(c)];
  }
  return mMap.Get(c);
}

void
DistVector::SetDiff(PRUnichar c, PRInt32 val)
{
  if (IsASCII(c)) {
    mArray[GetASCII(c)] = val;
  }
  mMap.Put(c, val);
}

static PLDHashOperator
PrintVector(const PRUnichar& aKey, PRInt32 aData, void *aUserArg)
{
  printf("%lc: %d\n", aKey, aData);
  return PL_DHASH_NEXT;
}


void
DistVector::Print()
{
  printf("DIFFVEC\n");
  for (int i = 0; i < 128; i++)
    if (mArray[i] > 0) {
      printf("arr -- %c: %d\n", i, mArray[i]);
    }
  mMap.EnumerateRead(PrintVector, this);
}

#define MIN(a, b) std::min((a), (b))
#define MIN3(a, b, c) std::min(std::min((a), (b)), (c))
#define MAX_DIST 100000000
#define dist(a, b) dist[(a)*(slen+1)+(b)]
#define NONE 4
#define SUBST 1
#define DELETE 2
#define INSERT 3

bool printMatch = false;
bool printDistMatrix = false;
bool printApproxMatches = false;
bool printAllMatchSummary = false;
bool printAllMatches = false;

inline void
printDist(PRInt32 *dist, PRInt32 slen, PRInt32 plen)
{
  if (printDistMatrix) {
    PRInt32 i, j;
    printf("  *");
    for (j = 0; j <= slen; j++)
      printf("%5d", j);
    printf("\n");
    for (i = 0; i <= plen; i++) {
      printf("%3d", i);
      for (j = 0; j <= slen; j++) {
        if (dist(i, j) > 300 || dist(i, j) < -300) {
          printf("%5d", -1);
        } else {
          printf("%5d", dist(i, j));
        }
      }
      printf("\n");
    }
  }
}

inline int
getMatch(PRInt32 *dist, const nsAString& p, const nsAString& s,
         int sEnd, double thresh, PRUint8*& opmap)
{
  // If we need to print the match, then we need to create the
  // string s1 and p1 that are obtained by aligning the matching
  // portion of s with p, and inserting '-' characters where
  // insertions/deletions take place.
  //
  // If we dont need to print, then we need only compute the
  // beginning of the match, which is in "j" at the end of the loop below

  PRInt32 plen = p.Length();
  PRInt32 slen = s.Length();

  int i = plen, j = sEnd;
  int k = i+sEnd+1;
  int rv;

  PRUint8 *op = (PRUint8 *) nsMemory::Alloc(k+1);
  op[k--] = 0;

  while ((i > 0) && (j > 0)) {
    PRUnichar sc = s[j-1], pc = p[i-1];
    PRInt32 curDist = dist(i, j);
    if (curDist == dist(i-1, j) + DistMetric::GetDCost(pc)) {
      op[k] = DELETE;
      i--;
    } else if (curDist == dist(i, j-1) + DistMetric::GetICost(sc)) {
      op[k] = INSERT;
      j--;
    } else {
        if (DistMetric::GetSCost(pc, sc) == 0) {
              op[k] = NONE;
        } else {
          op[k] = SUBST;
        }
        i--; j--;
    }
    k--;
  }

  k++;
  rv = j;
  opmap = (PRUint8 *) strdup((const char *) &op[k]);
  nsMemory::Free(op);
  return rv;
}

inline void
prtMatch(PRInt32 b, PRInt32 e, double d, const nsAString& p, const nsAString& s,
         const PRUint8* op, bool printMatch)
{
  int i, j; bool done;
  if (printMatch) {
	printf("distance(s[%d-%d]): %g\n\tp: ", b, e, d);
	for (i=0, j=0, done=false; (!done); j++) {
	  switch (op[j]) {
	  case NONE:
	  case SUBST:
		putwchar(p[i]); i++; continue;
	  case DELETE:
		putwchar(p[i]); i++; continue;
	  case INSERT:
		putchar('-'); continue;
	  default:
		done=true; break;
	  }
	}
	printf("\n\ts: ");
	for (i=b, j=0, done=false; (!done); j++) {
	  switch (op[j]) {
	  case NONE:
	  case SUBST:
		putwchar(s[i]); i++; continue;
	  case INSERT:
		putwchar(s[i]); i++; continue;
	  case DELETE:
		putchar('-'); continue;
	  default:
		done=true; break;
	  }
	}
	printf("\n\to: ");
	for (i=b, j=0, done=false; (!done); j++) {
	  switch (op[j]) {
	  case NONE:
		putchar('N'); continue;
	  case SUBST:
		putchar('S'); continue;
	  case INSERT:
		putchar('I'); continue;
	  case DELETE:
		putchar('D'); continue;
	  default:
		done=true; break;
	  }
	}
	printf("\n");
  }
}

//#define FIND_OVERLAPS

nsresult
P1Match(const nsAString& p, const nsAString& s, PRInt32 sOffset,
        double distThreshold, MatchRes& mres)
{
  PRInt32 i, j;
  PRInt32 sBeg, sEnd;
  PRInt32 bestDist=MAX_DIST;
  PRInt32 prevBest;
  PRInt32 distDn=0, distRt=0, distDiag=0;
  PRInt32* dist;
  PRUint8 *opmap;

  PRInt32 plen = p.Length();
  PRInt32 slen = s.Length();

  dist = (PRInt32 *) nsMemory::Alloc((plen+1)*(slen+1)*sizeof(PRInt32 *));
  if (dist == NULL) {
    NS_WARNING("dist is NULL!\n");
    return NS_ERROR_OUT_OF_MEMORY;
  }

  PRInt32 costThresh;

#ifdef DEBUG
  if (printAllMatchSummary) {
    printf("p1Match(p, s[%d-%d])\n", sOffset, sOffset+slen);
  }
#endif
  // Lazy Initialization of costThresh_
  if (mres.costThresh_ < 0) {
    PRInt32 maxCost = 0;
    prevBest = MAX_DIST;
    for (i = 0; i < plen; i++)
      maxCost += DistMetric::GetDCost(p[i]);
    costThresh = (PRInt32)floorf((float) distThreshold * maxCost);
    mres.costThresh_ = costThresh;
  } else {
    prevBest = mres.bestDist_;
    costThresh = mres.costThresh_;
  }

  // Initialize distance matrix

  dist(0, 0) = 0;
  for (j=1; j <= slen; j++)
    dist(0, j) = 0; // this is in the for loop...
  for (i=1; i <= plen; i++)
    dist(i, 0) = dist(i-1, 0) + DistMetric::GetDCost(p[i-1]);

#ifdef DEBUG
  printDist(dist, slen, plen);
#endif

  // Main loop: compute the minimum distance matrix

  for (i = 1; i <= plen; i++) {
    for (j = 1; j <= slen; j++) {
      PRUnichar sc = s[j-1], pc = p[i-1];
      distDn = dist(i-1, j) + DistMetric::GetDCost(pc);
      distRt = dist(i, j-1) + DistMetric::GetICost(sc);
      distDiag = dist(i-1, j-1) + DistMetric::GetSCost(pc, sc);
      dist(i, j) = MIN3(distDn, distRt, distDiag);
    }
  }

#ifdef DEBUG
  printDist(dist, slen, plen);
#endif
  /* Now, look for j such that dist(plen, j) is minimum. This gives
     the lowest cost substring match between p and s */

  sEnd = 1;
  for (j=1; j <= slen; j++) {
    if (dist(plen, j) < bestDist) {
      bestDist = dist(plen, j);
      sEnd = j;
    }
  }

  // Compare the best with previous best matches to see if there is any
  // sense in continuing further. We retain results that are below costThresh
  // that are within 50% of costThresh from the best result so far.

  // TODO: what do we need "bad" matches for in the first place?
  // we already know they won't be used by the caller, do we need them
  // for overlaps? Perhaps we can simply keep valid matches only, and
  // eventually merge them at the end if necessary.

  if (bestDist <= prevBest + (costThresh/2)) {
    PRInt32 bestSoFar = MIN(bestDist, prevBest);
    mres.bestDist_ = bestSoFar;

    if (bestDist < prevBest) {
      for (PRUint32 i = 0; i < mres.elem_.Length(); i++) {
        if ((mres[i].dist_ > costThresh) &&
            (mres[i].dist_ > bestDist+(costThresh/2))) {
#ifdef DEBUG
          printf("Removing worse match: [%d, %d]\n", mres[i].matchBeg_,
                 mres[i].matchEnd_);
#endif
          mres.elem_.RemoveElementAt(i);
          i--; // next loop goes back to i;
        }
      }
    }

    sBeg = getMatch(dist, p, s, sEnd, distThreshold, opmap);
#ifdef DEBUG
    prtMatch(sBeg, sEnd, bestDist, p, s, opmap, printMatch);
#endif
    // previously sEnd was decreased by 1. why?
    MatchResElem mre(bestDist, sBeg + sOffset, sEnd + sOffset, opmap);
    mres.elem_.AppendElement(mre);

    // Now, compare the best match with other possible matches
    // identified in this invocation of this function

#ifdef FIND_OVERLAPS
    PRInt32 l;
    for (l=1; l <= slen; l++) {
      PRInt32 currDist  = dist(plen, l);
      if (((currDist <= costThresh) ||
           (currDist <= bestSoFar + (costThresh/2))) &&
          // The first two tests below eliminate consideration
          // of distances that do not correspond to local minima
          (currDist < dist(plen, l-1)) &&
          ((l == slen) || currDist < dist(plen, l+1)) &&
          (l != sEnd)) /* Dont consider the global minima, either */ {

        j = getMatch(dist, p, s, l, distThreshold, opmap);

        /* s[j]...s[l-1] are included in the match with p */

        // Eliminate matches that have large overlap with the
        // main match.  This is necessary since the "non-local
        // minima" test earlier misses situations where the
        // distance increases briefly but then decreases after
        // a few more characters. In that case, you seem to
        // have a local minima, but the corresponding match is
        // subsumed in the ultimate match that is discovered.

        PRInt32 uniqLen=0;
        if (j < sBeg) {
          uniqLen += sBeg-j;
        }
        if (l > sEnd) {
          uniqLen += l-sEnd;
        }
        if (uniqLen > (plen*distThreshold)) {
          // TODO: why are we only adding a match and not deleting the
          // previous matches?
          MatchResElem mre(bestDist, j + sOffset, l-1 + sOffset, opmap);
          mres.elem_.InsertElementAt(0, mre);
          printf("prtMatch, find overlap\n");
          prtMatch(j, l, dist(plen, l), p, s, opmap, printAllMatches);
        }
      } // if ((currDist <= costThresh) ...
    } // for (l=1; ...
#endif
  } // if (bestDist <= ...
  nsMemory::Free(dist);
  return NS_OK;

}

inline void
adjustDiffIns(PRUnichar c, DistVector& vec, int& idiff, int& ddiff)
{
  if (vec.GetDiff(c) > 0) {
    // an excess of c in p, now reduced when c is added to s
    ddiff -= DistMetric::GetDCost(c);
  } else {
    // an excess of c in s, now excess further increased
    idiff += DistMetric::GetICost(c);
  }
  vec.SetDiff(c, vec.GetDiff(c) - 1);
}

inline void
adjustDiffDel(PRUnichar c, DistVector& vec, int& idiff, int& ddiff)
{
  if (vec.GetDiff(c) >= 0) {
    // an excess of c in p, further increased now
    ddiff += DistMetric::GetDCost(c);
  } else {
    // an excess of c in s, excess being reduced now, so decrease
    idiff -= DistMetric::GetICost(c); // insertion cost correspondingly.
  }
  vec.SetDiff(c, vec.GetDiff(c) + 1);
}

nsresult
nsXSSUtils::P1FastMatch(const nsAString& p, const nsAString& s,
                        double distThreshold, MatchRes& mres)
{
  PRInt32 plen = p.Length();
  PRInt32 slen = s.Length();

  if (plen > slen) {
    NS_ERROR("p should be smaller or as long as s");
    printf("p (%d): %s\n", plen, NS_LossyConvertUTF16toASCII(p).get());
    printf("s (%d): %s\n", slen, NS_LossyConvertUTF16toASCII(s).get());
    return NS_ERROR_INVALID_ARG;
  }

  if (p.IsEmpty()) {
    return NS_OK;
  }


  PRInt32 idiff=0, ddiff=0, diff;
  PRInt32 diffThresh;
  PRInt32 start=-1, end=-1;
  DistVector diffVec;
  const PRUnichar* cur = p.BeginReading();
  const PRUnichar* finish = p.EndReading();

  mres.costThresh_ = -1;
  mres.bestDist_ = MAX_DIST;

  for (; cur < finish; ++cur) {
    PRUnichar c = DistMetric::GetNorm(*cur);
    adjustDiffDel(c, diffVec, idiff, ddiff);
  }

  diffThresh = (PRInt32)floor(ddiff*distThreshold);


  for (PRInt32 i = 0; i < plen; ++i) {
    PRUnichar d = DistMetric::GetNorm(s[i]);
    adjustDiffIns(d, diffVec, idiff, ddiff);
  }

  //diffVec.print();

  diff = MIN(idiff, ddiff);

  if (diff <= diffThresh) {
    start = 0;
  }

  // move the sliding window, and when the difference is low enough,
  // call the actual P1Match on the candidate.
  for (PRInt32 j=plen; j < slen; j++) {
    PRUnichar c = DistMetric::GetNorm(s[j-plen]);
    PRUnichar d = DistMetric::GetNorm(s[j]);

    adjustDiffDel(c, diffVec, idiff, ddiff);
    adjustDiffIns(d, diffVec, idiff, ddiff);
    diff = MIN(idiff, ddiff);

    if (start == -1) {
      if (diff <= diffThresh) {
        start = j-plen+1;
      }
    } else if (diff > diffThresh) {
      end = j+1;//-1+diffThresh;
      if (end >= slen) {
        end = slen;
      }
      start -= 1;//diffThresh;
      if (start < 0) {
        start = 0;
      }

      const nsAString& newS = Substring(s, start, end-start+1);
      nsresult rv = P1Match(p, newS, start, distThreshold, mres);
      NS_ENSURE_SUCCESS(rv, rv);
      start = -1;
      //j = end-1;
    }
  }

  // this is for the remaining of the string if you started seeing a
  // match and you get to the end of the string before the end of
  // the match.
  if (start != -1) {
    end = slen-1;

    // increasing 1 seems to be ok here... it is a bit confusing
    // because we print the values and they don't always have the
    // same semantic meaning. sometimes they are "last character
    // in the match" and sometimes "first char out of the match"
    const nsAString &newS = Substring(s, start, end-start+1);
    nsresult rv = P1Match(p, newS, start, distThreshold, mres);
    NS_ENSURE_SUCCESS(rv, rv);
  }
#ifdef DEBUG
  if (printApproxMatches || printAllMatchSummary) {
    if (!mres.elem_.IsEmpty() && (mres.bestDist_ <= mres.costThresh_) &&
        (printAllMatchSummary
         || ((mres.bestDist_ > 0) || (mres.elem_.Length() != 1)))) {
      for (PRUint32 i = 0; i < mres.elem_.Length(); i++) {
        printf("prtMatch, printMatchsummary\n");
        prtMatch(mres[i].matchBeg_, mres[i].matchEnd_,
                 mres[i].dist_, p, s, mres[i].opmap_, true);
      }

      for (PRUint32 i = 0; i < mres.elem_.Length(); i++) {
        PRInt32 smin = mres[i].matchBeg_ - 6;
        PRInt32 smax = mres[i].matchEnd_ + 6;
        printf("MATCH:\n");
        if (smin > 0) {
          printf("...");
        } else {
          printf("   ");
        }

        for (PRInt32 k=smin; k <= MIN(smax, slen-1); k++) {
          if (k >= 0) {
            if ((s[k] == PRUnichar('\n')) ||
                (s[k] == PRUnichar('\t'))) {
              printf(" ");
            } else {
              putwchar(s[k]);
            }
          } else {
            printf(" ");
          }
        }
        if (smax < slen-1) {
          printf("...");
        }
        printf("[%d,%d]", mres[i].matchBeg_, mres[i].matchEnd_);
        if (mres[i].dist_ != 0) {
          printf(": %f", (mres[i].dist_*distThreshold)/mres.costThresh_);
        }
        printf("\n");
        if (mres.costThresh_ != 0) {
          printf("Relative Distance: %f%%\n",
                 (mres[i].dist_*distThreshold*100)/mres.costThresh_);
          printf("dist: %d\n", mres[i].dist_);
          printf("distThreshold: %f\n", distThreshold);
          printf("costThresh: %d\n", mres.costThresh_);
        }
      }
    }
    if (mres.elem_.IsEmpty() || (mres.bestDist_ > mres.costThresh_)) {
      printf("p1FastMatch found no good matches.\n");
    }
  }
#endif // DEBUG

  return NS_OK;

}

nsresult
nsXSSUtils::P1FastMatchReverse(const nsAString& s, const nsAString& p,
                               double distThreshold, MatchRes& mres)
{
  nsresult rv;
  rv = nsXSSUtils::P1FastMatch(p, s, distThreshold, mres);
  NS_ENSURE_SUCCESS(rv, rv);

  // we need to take care of both deletion and substitution (though
  // the current costs always make deletion cheaper)
  for (PRUint32 i = 0; i < mres.elem_.Length(); i++) {
    PRUint32 len = strlen((char*) mres[i].opmap_);
    PRUint32 beg;
    for (beg = 0; beg < len; beg++)
      if (mres[i].opmap_[beg] != SUBST &&
          mres[i].opmap_[beg] != DELETE) {
        break;
      }
    PRUint32 end;
    for (end = len; end > 0; end--) {
      if (mres[i].opmap_[end-1] != SUBST &&
          mres[i].opmap_[end-1] != DELETE) {
        break;
      }
    }
    mres[i].matchBeg_ = beg;
    mres[i].matchEnd_ = end;
  }
  return NS_OK;

}


/************************
 * XPCOM Module stuff
 ************************/

nsXSSUtilsWrapper* nsXSSUtilsWrapper::gXSSUtilsWrapper = nullptr;

NS_IMPL_THREADSAFE_ISUPPORTS1(nsXSSUtilsWrapper, nsIXSSUtils)

nsXSSUtilsWrapper*
nsXSSUtilsWrapper::GetSingleton()
{
  if (!gXSSUtilsWrapper) {
    gXSSUtilsWrapper = new nsXSSUtilsWrapper();
  }

  NS_ADDREF(gXSSUtilsWrapper);
  return gXSSUtilsWrapper;
}

/********************
 * TESTS
 ********************/


/*** Macros and utils ***/

static PRUint32 gFailCount = 0;

#define PASS(_msg) printf("TEST-PASS | %s\n", _msg);
#define FAIL(_msg) printf("TEST-UNEXPECTED-FAIL | %s\n", _msg); ++gFailCount;

#define TEST_ENSURE_COND(_test, _msg)			\
  if (_test) {                            \
    PASS(_msg);                           \
  } else {                                \
    FAIL(_msg);                           \
  }

/*** Testing structures ***/

struct stringBool
{
  const char* val;
  bool rv;
};

struct twoStrings
{
  const char *str1;
  const char *str2;
};

struct twoStringsBool
{
  const char *p;
  const char *s;
  bool rv;
};

struct twoChars
{
  const PRUnichar a,b;
};

struct stringInt
{
  const char *str;
  const PRInt32 i;
};

struct staticParam
{
  const char *name, *value;
  bool dangerous, special;
  Parameter ToParam() const
  {
    return Parameter(name, value, dangerous, special);
  };
};
/**** Testing data ****/

const twoStrings suspiciousPathData[] = {
  { "hello", ""},
  { "watch/video", ""},
  { "subdir/index_en.php", ""},
  { "search/<script>/puppa.html", "<script>"},
  { "page/'\"><script>alert(1)</script>/44/", "'\"><script>alert(1)</script>"},
  {"/pages/static/<script>alert(2)/fake/</script>/index.html",
   "<script>alert(2)/fake/</script>"},
  {"a=b", "="},
  {"/src/file_depot/mozilla-central/b.php", ""}
};

const twoStrings unescapeData[] = {
  { "hello", "hello" },
  { "hello%20world", "hello world" },
  { "hello%2520world", "hello world" },
  { "a=3&b=a%26b", "a=3&b=a&b" },
  { "", "" }
};

const twoStrings domainData[] = {
  { "http://www.google.com", "google.com" },
  { "https://google.co.uk/index.html?a=5", "google.co.uk" },
  { "http://lab.cs.cam.ac.uk/dir/a.php#aaa", "cam.ac.uk" },
  { "http://a:b@nothing.asfd-fdfd.co.jp", "asfd-fdfd.co.jp" },
  { "https://seclab.cs.sunysb.edu:8080/faculty/index.html", "sunysb.edu" },
};

const char *uri1 = "http://www.a.com";
const staticParam* uriData1 = nullptr;
const char *uri2 = "http://www.a.com/index.php?a=3";
const staticParam* uriData2 = nullptr;
const char *uri3 =
  "http://www.a.com/index.php?a=3&b=aaaaa&df=<script>xss()</script>";
const staticParam uriData3[] = {
  {"URL", "index.php?a=3&b=aaaaa&df=<script>xss()</script>", true, true},
};
const char *uri4 =
  "http://www.a.com/index.php?a=3&b=<script>alert(1)</script>#hello";
const staticParam uriData4[] = {
  {"URL", "index.php?a=3&b=<script>alert(1)</script>#hello", true, true}
};
const char *uri5 = "http://www.a.com/dir/adf_en.html?a=4#hi";
const staticParam* uriData5 = nullptr;
const char *uri6 =
  "http://www.a.com/dir/<script>/index.php?param_1=<script>#<hello>aa";
const staticParam uriData6[] = {
  {"URL", "dir/<script>/index.php?param_1=<script>#<hello>aa",
   true, true}
};
const char *uri7 =
  "http://www.a.com/index.php;param<script>param";
const staticParam uriData7[] = {
  {"URL", "index.php;param<script>param", true, true}
};



const char *p1pStr1 = "hello";
const char *p1sStr1 = "hello";
const MatchResElem p1Data1[] = {
  MatchResElem(0, 0, 5, nullptr),
};
const char *p1pStr2 = "hello";
const char *p1sStr2 = "helloworldhello";
const MatchResElem p1Data2[] = {
  MatchResElem(0, 0, 5, nullptr),
  MatchResElem(0, 10, 15, nullptr),
};
const char *p1pStr3 = "hello";
const char *p1sStr3 = "aaaaahellobbbbbbbbbbbbbb";
const MatchResElem p1Data3[] = {
  MatchResElem(0, 5, 10, nullptr),
};
const char *p1pStr4 = "ahello";
const char *p1sStr4 = "zzzhallozzz";
const MatchResElem* p1Data4 = nullptr;

const char *p1pStr5 = "lowerCaseStuff";
const char *p1sStr5 = "this string has LOWERCASESTUFF";
const MatchResElem p1Data5[] = {
  MatchResElem(0, 16, 30, nullptr),
};
const char *p1pStr6 = "abc'; alert('xss'); //";
const char *p1sStr6 = "var q = 'abc'; alert('xss'); //';";
const MatchResElem p1Data6[] = {
  MatchResElem(0, 9, 31, nullptr),
};

// reverse stuff
const char *p1pStr7 = "<script>alert('xss')</script>";
const char *p1sStr7 = "alert('xss');";
const MatchResElem p1Data7[] = {
  MatchResElem(20, 0, 12, nullptr),
};
const char *p1pStr8 = "<script>alert('xss')</script>";
const char *p1sStr8 = "bbalert('xss')a";
const MatchResElem p1Data8[] = {
  MatchResElem(60, 2, 14, nullptr),
};
const char *p1pStr9 = "<script>alert('xss')</script>";
const char *p1sStr9 = "blert('xss']";
const MatchResElem p1Data9[] = {
  MatchResElem(40, 1, 11, nullptr),
};
const char *p1pStr10 = "<script>alert(\"xss attack\");</script>";
const char *p1sStr10 = "alert('xss attack');";
const MatchResElem p1Data10[] = {
  MatchResElem(60, 0, 20, nullptr),
};

const twoStringsBool findInlineData[] = {
  { "<script>alert('xss')</script>", "alert('xss')", true },
  { "index<dff>.php?<script>alert(2);</script>", "alert(2);", true },
  { "index<dff>.php?<script>alert(2);</script>#<script>pippo()</script>",
    "xss()", false },
  { "abav' onmouseover='alert(\"xss\")'", "alert(\"xss\")", true}
};

const twoStringsBool findExternalData[] = {
  { "<script src='http://evil.com/helloworld.js'></script>",
    "http://evil.com/helloworld.js", true },
  { "<script src='http://othersite.co.uk/helloworld.js'></script>",
    "http://evil.com/helloworld.js", false }
};

//TODO: create a test that works with findoverlaps

/*** Tests ***/
nsresult
TestLowerCaseASCII()
{
  TEST_ENSURE_COND( ASCIIToLowerCase(PRUnichar(L'a')) == PRUnichar(L'a'),
                    "ASCIIToLowerCase" );
  TEST_ENSURE_COND( ASCIIToLowerCase(PRUnichar(L'A')) == PRUnichar(L'a'),
                    "ASCIIToLowerCase");
  TEST_ENSURE_COND( ASCIIToLowerCase(PRUnichar(L' ')) == PRUnichar(L' '),
                    "ASCIIToLowerCase" );
  TEST_ENSURE_COND( ASCIIToLowerCase(PRUnichar(L'Ł')) == PRUnichar(L'Ł'),
                    "ASCIIToLowerCase");
  return NS_OK;
}


// requires s, uri, ios and params to be defined
#define TestOneURL(__str, __data)                                       \
  s.Assign(__str);                                                      \
  ios->NewURI(s, nullptr, nullptr, getter_AddRefs(uri));                  \
  nsXSSUtils::ParseURI(uri, params, nullptr);                            \
  if (__data) {                                                         \
    for (unsigned int i = 0; i < sizeof(__data) / sizeof(staticParam); i++) { \
      TEST_ENSURE_COND(__data[i].ToParam() == params[i],                \
                       "ParseURI: " #__data);                           \
    }                                                                   \
    TEST_ENSURE_COND(params.Length() == sizeof(__data) / sizeof(staticParam), \
                     "ParseURI Len: " #__data);                         \
  } else {                                                              \
    TEST_ENSURE_COND(params.IsEmpty(), "ParseURI: " #__data);           \
  }                                                                     \
  params.Clear();

nsresult
TestParseURI()
{
  ParameterArray params;
  nsCOMPtr<nsIIOService> ios = do_GetIOService();
  nsCOMPtr<nsIURI> uri;
  nsAutoCString s;
  TestOneURL(uri1, uriData1);
  TestOneURL(uri2, uriData2);
  TestOneURL(uri3, uriData3);
  TestOneURL(uri4, uriData4);
  TestOneURL(uri5, uriData5);
  TestOneURL(uri6, uriData6);
  TestOneURL(uri7, uriData7);
  return NS_OK;
}

nsresult
TestUnescapeLoop()
{
  for (unsigned int i = 0; i < sizeof(unescapeData) / sizeof(twoStrings); i++) {
    nsAutoString s;
    s = NS_ConvertASCIItoUTF16(unescapeData[i].str1);
    nsXSSUtils::UnescapeLoop(s, s, nullptr);
    TEST_ENSURE_COND(s.Equals(NS_ConvertASCIItoUTF16(unescapeData[i].str2)),
                     "UnescapeLoop");
  }
  return NS_OK;
}

#define TestTrimMacro(word, chars)                                      \
nsresult TestTrim##word() {                                             \
  for (unsigned int i = 0;                                              \
       i < sizeof(suspicious##word##Data) / sizeof(twoStrings); i++) {  \
    nsAutoString s;                                                     \
    s = NS_ConvertASCIItoUTF16(suspicious##word##Data[i].str1);         \
    nsXSSUtils::TrimSafeChars(s, s, chars);                             \
    TEST_ENSURE_COND(s.Equals(                                          \
        NS_ConvertASCIItoUTF16(suspicious##word##Data[i].str2)),        \
                     "Trim word");                                      \
  }                                                                     \
  return NS_OK;                                                         \
}

TestTrimMacro(Path, PATH_CHARS);
//TestTrimMacro(Ref, REF_CHARS); similar enough, don't bother
//TestTrimMacro(Query, QUERY_CHARS);



nsresult
TestGetDomain()
{
  nsCOMPtr<nsIIOService> ios = do_GetIOService();
  nsCOMPtr<nsIURI> uri;
  for (unsigned int i = 0; i < sizeof(domainData) / sizeof(twoStrings); i++) {
    nsAutoCString s(domainData[i].str1);
    ios->NewURI(s, nullptr, nullptr, getter_AddRefs(uri));
    nsAutoString result;
    nsXSSUtils::GetDomain(uri, result);
    TEST_ENSURE_COND(result.Equals(NS_ConvertASCIItoUTF16(domainData[i].str2)), "GetDomain");
  }
  return NS_OK;
}


nsresult
TestDistVector()
{
  DistVector v;
  v.SetDiff(PRUnichar('a'), 2);
  v.SetDiff(PRUnichar('a'), 0);
  v.SetDiff(PRUnichar(L'Ł'), 3);
  v.SetDiff(PRUnichar(L'Ɖ'), -1);
  v.SetDiff(PRUnichar('f'), 8);
  TEST_ENSURE_COND(v.GetDiff(PRUnichar('a')) == 0, "DistVector");
  TEST_ENSURE_COND(v.GetDiff(PRUnichar(L'Ł')) == 3, "DistVector");
  TEST_ENSURE_COND(v.GetDiff(PRUnichar(L'Ɖ')) == -1, "DistVector");
  TEST_ENSURE_COND(v.GetDiff(PRUnichar('f')) == 8, "DistVector");
  TEST_ENSURE_COND(DistVector::IsASCII(PRUnichar('a')) == true, "DistVector");
  TEST_ENSURE_COND(DistVector::IsASCII(PRUnichar(L'Ɖ')) == false, "DistVector");
  TEST_ENSURE_COND(DistVector::IsASCII(PRUnichar('F')) == true, "DistVector");
  TEST_ENSURE_COND(DistVector::IsASCII(PRUnichar(L'Ł')) == false, "DistVector");
  return NS_OK;
}



// requires p, s, mres to be defined
#define TestOneP1(__p, __s, __data)                                     \
  mres.costThresh_ = -1;                                                \
  mres.bestDist_ = MAX_DIST;                                            \
  mres.elem_.Clear();                                                   \
  p.Assign(NS_ConvertASCIItoUTF16(__p));                                \
  s.Assign(NS_ConvertASCIItoUTF16(__s));                                \
  P1Match(p, s, 0, THRESHOLD, mres);                                    \
  mres.ClearInvalidMatches(THRESHOLD);                                  \
  if (__data) {                                                         \
    for (unsigned int i = 0; i < sizeof(__data) / sizeof(MatchResElem); i++) \
      TEST_ENSURE_COND(__data[i] == mres[i], "P1Match");                \
    TEST_ENSURE_COND(mres.elem_.Length() == sizeof(__data) / sizeof(MatchResElem), \
                     "P1Match");                                        \
  } else {                                                              \
    TEST_ENSURE_COND(mres.elem_.IsEmpty(), "P1Match");                  \
  }

nsresult
TestP1Match()
{
  MatchRes mres;
  nsAutoString p, s;
  TestOneP1(p1pStr1, p1sStr1, p1Data1);
  //TestOneP1(p1pStr2, p1sStr2, p1Data2);
  TestOneP1(p1pStr3, p1sStr3, p1Data3);
  TestOneP1(p1pStr4, p1sStr4, p1Data4);
  TestOneP1(p1pStr5, p1sStr5, p1Data5);
  TestOneP1(p1pStr6, p1sStr6, p1Data6);
  return NS_OK;
}

// requires p, s, mres to be defined
#define TestOneP1Fast(__p, __s, __data)                                 \
  mres.elem_.Clear();                                                   \
  p.Assign(NS_ConvertASCIItoUTF16(__p));                                \
  s.Assign(NS_ConvertASCIItoUTF16(__s));                                \
  nsXSSUtils::P1FastMatch(p, s, THRESHOLD, mres);                       \
  mres.ClearInvalidMatches(THRESHOLD);                                  \
  if (__data) {                                                         \
    for (unsigned int i = 0; i < sizeof(__data) / sizeof(MatchResElem); i++) \
      TEST_ENSURE_COND(__data[i] == mres[i], "P1FastMatch");            \
    TEST_ENSURE_COND(mres.elem_.Length() == sizeof(__data) / sizeof(MatchResElem), \
                     "P1FastMatch");                                    \
  } else {                                                              \
    TEST_ENSURE_COND(mres.elem_.IsEmpty(), "P1FastMatch");              \
  }

nsresult
TestP1FastMatch()
{
  MatchRes mres;
  nsAutoString p, s;
  TestOneP1Fast(p1pStr1, p1sStr1, p1Data1);
  TestOneP1Fast(p1pStr2, p1sStr2, p1Data2);
  TestOneP1Fast(p1pStr3, p1sStr3, p1Data3);
  TestOneP1Fast(p1pStr4, p1sStr4, p1Data4);
  TestOneP1Fast(p1pStr5, p1sStr5, p1Data5);
  TestOneP1Fast(p1pStr6, p1sStr6, p1Data6);
  // a couple of tests using unicode chars
  mres.elem_.Clear();
  p.Assign(NS_LITERAL_STRING("您好世界您好世界您好世界"));
  s.Assign(NS_LITERAL_STRING("您好世界您好世界您好世界"));
  nsXSSUtils::P1FastMatch(p, s, THRESHOLD, mres);
  mres.ClearInvalidMatches(THRESHOLD);
  TEST_ENSURE_COND(mres[0].dist_ == 0, "p1FastMatch.Unicode1");
  mres.elem_.Clear();
  // it seems that msvc++ does not understand unicode characters
  // directly in source files.
#ifndef _WINDOWS
  p.Assign(NS_LITERAL_STRING("您好世界您好世界您好世界"));
  s.Assign(NS_LITERAL_STRING("您好a界您好世界您好世界"));
  nsXSSUtils::P1FastMatch(p, s, THRESHOLD, mres);
  mres.ClearInvalidMatches(THRESHOLD);
  TEST_ENSURE_COND(mres[0].dist_ == 30, "p1FastMatch.Unicode2");
  mres.elem_.Clear();
  p.Assign(NS_LITERAL_STRING("您好世界您好世界世界"));
  s.Assign(NS_LITERAL_STRING("您好世界您好世界您好世界"));
  nsXSSUtils::P1FastMatch(p, s, THRESHOLD, mres);
  mres.ClearInvalidMatches(THRESHOLD);
  TEST_ENSURE_COND(mres[0].dist_ == 40, "p1FastMatch.Unicode3");
#endif
  return NS_OK;
}

// requires p, s, mres to be defined
#define TestOneP1FastReverse(__p, __s, __data)                          \
  mres.elem_.Clear();                                                   \
  p.Assign(NS_ConvertASCIItoUTF16(__p));                                \
  s.Assign(NS_ConvertASCIItoUTF16(__s));                                \
  nsXSSUtils::P1FastMatchReverse(p, s, THRESHOLD, mres);                \
  mres.ClearInvalidMatches(THRESHOLD);                                  \
  if (__data) {                                                         \
    for (unsigned int i = 0; i < sizeof(__data) / sizeof(MatchResElem); i++) \
      TEST_ENSURE_COND(__data[i] == mres[i], "P1FastMatchReverse");      \
    TEST_ENSURE_COND(mres.elem_.Length() == sizeof(__data) / sizeof(MatchResElem), \
                     "P1FastMatchReverse");                             \
  } else {                                                              \
    TEST_ENSURE_COND(mres.elem_.IsEmpty(), "P1FastMatchReverse");       \
  }

nsresult
TestP1FastMatchReverse()
{
  MatchRes mres;
  nsAutoString p, s;
  TestOneP1FastReverse(p1pStr7, p1sStr7, p1Data7);
  TestOneP1FastReverse(p1pStr8, p1sStr8, p1Data8);
  TestOneP1FastReverse(p1pStr9, p1sStr9, p1Data9);
  TestOneP1FastReverse(p1pStr10, p1sStr10, p1Data10);
  return NS_OK;
}

nsresult
TestFindInlineXSS()
{
  for (unsigned int i = 0; i < sizeof(findInlineData) / sizeof(twoStringsBool); i++) {
    nsAutoString p, s;
    bool rv;
    p = NS_ConvertASCIItoUTF16(findInlineData[i].p);
    s = NS_ConvertASCIItoUTF16(findInlineData[i].s);
    if (p.Length() > s.Length()) {
      rv = nsXSSUtils::FindInlineXSS(p, s);
    } else {
      rv = nsXSSUtils::FindInlineXSS(p, s);
    }
    TEST_ENSURE_COND(rv == findInlineData[i].rv, "FindInlineXSS");
  }
  return NS_OK;
}

nsresult
TestFindExternalXSS()
{
  nsCOMPtr<nsIURI> uri;
  nsCOMPtr<nsIIOService> ios = do_GetIOService();
  for (unsigned int i = 0;
       i < sizeof(findExternalData) / sizeof(twoStringsBool); i++) {
    nsAutoString p;
    nsAutoCString u;
    bool rv;
    p.Assign(NS_ConvertASCIItoUTF16(findExternalData[i].p));
    u.Assign(findExternalData[i].s);
    ios->NewURI(u, nullptr, nullptr, getter_AddRefs(uri));
    if (p.Length() > u.Length()) {
      rv = nsXSSUtils::FindExternalXSS(p, uri);
    } else {
      rv = nsXSSUtils::FindExternalXSS(p, uri);
    }
    TEST_ENSURE_COND(rv == findExternalData[i].rv, "FindExternalXSS");
  }
  return NS_OK;
}

// these two don't need much testing, this path is taken enough by mochitests
nsresult
TestCheckInline()
{
  nsAutoString script;
  ParameterArray params;

  // reversed
  script.Assign(NS_LITERAL_STRING("alert('xss attack')"));
  params.AppendElement(Parameter("URL", "<script>hello world</script>",
                                 true, true));
  nsXSSUtils::CheckInline(script, params);
  TEST_ENSURE_COND(params[0].attack == false, "CheckInline");
  params.Clear();

  script.Assign(NS_LITERAL_STRING("alert('xss attack')"));
  params.AppendElement(Parameter("URL", "<script>alert(\"xss attack\")</script>",
                                 true, true));
  nsXSSUtils::CheckInline(script, params);
  TEST_ENSURE_COND(params[0].attack == true, "CheckInline");
  params.Clear();

  return NS_OK;

}

nsresult
TestCheckExternal()
{
  nsAutoCString u, pU;
  ParameterArray params;
  nsCOMPtr<nsIURI> uri, pageUri;
  nsCOMPtr<nsIIOService> ios = do_GetIOService();

  pU.Assign(NS_LITERAL_CSTRING("http://www.localhost.net/pages/stuff.index.html"));
  ios->NewURI(pU, nullptr, nullptr, getter_AddRefs(pageUri));

  u.Assign(NS_LITERAL_CSTRING("http://www.google.com/scripts/script.js"));
  ios->NewURI(u, nullptr, nullptr, getter_AddRefs(uri));
  params.AppendElement(
    Parameter("URL", "<script src='http://www.google.com/helloworld/gino.js'></script>",
              true, true));
  nsXSSUtils::CheckExternal(uri, pageUri, params);
  TEST_ENSURE_COND(params[0].attack == false, "CheckExternal");
  params.Clear();

  u.Assign(NS_LITERAL_CSTRING("http://hello.localhost.net/scripts/script.js"));
  ios->NewURI(u, nullptr, nullptr, getter_AddRefs(uri));
  params.AppendElement(Parameter("a", "<script src='hello.localhost.net/scripts/script.js'></script>",
                                 true, false));
  nsXSSUtils::CheckExternal(uri, pageUri, params);
  TEST_ENSURE_COND(params[0].attack == false, "CheckExternal");
  params.Clear();

  u.Assign(NS_LITERAL_CSTRING("http://www.pippo.com/script.js"));
  ios->NewURI(u, nullptr, nullptr, getter_AddRefs(uri));
  params.AppendElement(Parameter("URL1", "<script src='http://www.pippo.com/script.js'></script>", true, true));
  params.AppendElement(Parameter("URL2", "<script src='http://www.localhost.com/script.js'></script>", true, true));
  nsXSSUtils::CheckExternal(uri, pageUri, params);
  TEST_ENSURE_COND(params[0].attack == true, "CheckExternal");
  TEST_ENSURE_COND(params[1].attack == false, "CheckExternal");

  return NS_OK;
}

nsresult
nsXSSUtilsWrapper::Test()
{
#ifdef PR_LOGGING
  gXssPRLog = PR_NewLogModule("XSS");
#endif
  TestLowerCaseASCII();
  TestTrimPath();
  TestParseURI();
  TestUnescapeLoop();
  TestGetDomain();
  TestDistVector();
  TestP1Match();
  TestP1FastMatch();
  TestP1FastMatchReverse();
  TestFindInlineXSS();
  TestFindExternalXSS();
  TestCheckInline();
  TestCheckExternal();
  if (gFailCount > 0) {
    printf("TESTS FAILED\n");
    printf("%d failures\n", gFailCount);
    return NS_ERROR_BASE;
  }
  return NS_OK;
}
