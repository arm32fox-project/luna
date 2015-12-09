/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsXSSUtils_h
#define nsXSSUtils_h

#include "prlog.h"
#include "nsTArray.h"
#include "nsString.h"
#include "nsIXSSUtils.h"
#include "nsDataHashtable.h"

#ifdef PR_LOGGING
#define LOG_XSS_CALL(f) PR_LOG(gXssPRLog, PR_LOG_DEBUG, (">>>>> %s", f));
#define LOG_XSS(msg) PR_LOG(gXssPRLog, PR_LOG_DEBUG, ("XSS: " msg));
#define LOG_XSS_1(msg, arg) PR_LOG(gXssPRLog, PR_LOG_DEBUG, \
                                   ("XSS: " msg, arg));
#define LOG_XSS_2(msg, arg1, arg2) PR_LOG(gXssPRLog, PR_LOG_DEBUG, \
                                          ("XSS: " msg, arg1, arg2));
#else
#define LOG_XSS_CALL(f) ;
#define LOG_XSS(msg) ;
#define LOG_XSS_1(msg, arg) ;
#define LOG_XSS_2(msg, arg1, arg2) ;
#endif

class nsIDocument;

/**
 * This class is used to store the various sources of XSS attacks for
 * each HTTP request and the result of the XSS checks again a particular
 * script. Currently, at most two parameters are being inserted into
 * the filter: the url of the webpage and the body of POST
 * requests. Look at ParseURI and ParsePOST for the actual logic.  An
 * earlier iteration of the filter would break down the URL into many
 * different parameters, and this class still supports that if needed.
 * I think the dangerous and special fields are not actively used anymore.
 */
struct Parameter {
  nsString name, value;
  bool attack, dangerous, special;
  Parameter(const nsAString& aName, const nsAString& aValue,
            bool aDangerous, bool aSpecial, bool aAttack = false);
  Parameter(const char* aName, const char* aValue,
            bool aDangerous, bool aSpecial, bool aAttack = false);
  // for debugging
  bool operator==(const Parameter& other) const;
  void print() const;
};

class nsIURI;

typedef nsTArray<Parameter> ParameterArray;

/**
 * MatchRes, DistMetric and DistVector are used by the approximate
 * substring matching algorithm, which is implemented in
 * p1FastMatch/p1Match.
 */
class MatchResElem {
 public:
  PRInt32 dist_;		// edit distance
  PRUint32 matchBeg_;	// start and
  PRUint32 matchEnd_;	//   end of match
  PRUint8 *opmap_;		// table of edit operator costs

 public:
  MatchResElem(PRUint32 d, PRUint32 b, PRUint32 e, PRUint8 *opmap)
	{dist_ = d; matchBeg_ = b; matchEnd_ = e; opmap_ = opmap; };
  bool operator==(const MatchResElem& other) const
  { return dist_ == other.dist_ && matchBeg_ == other.matchBeg_ &&
      matchEnd_ == other.matchEnd_; }; // don't compare opmap for testing
  // are we leaking mem with opmap?
};


typedef nsTArray<MatchResElem> Matches;
/**
 * Represents a Match Result as returned by p1FastMach.
 */
class MatchRes {
 public:
  PRInt32 costThresh_;	// minimum edit distance for matches
  PRInt32 bestDist_;	// best obseved edit distance in the matches found
  Matches elem_;	// list of matches found
  bool HasMatches() { return !elem_.IsEmpty(); };
  MatchResElem& operator[] (PRUint32 i) { return elem_.ElementAt(i); };
  PRUint32 Length() const { return elem_.Length(); };
  void ClearInvalidMatches(double threshold);
};

class DistMetric {
 public:
  static PRUint8 GetICost(PRUnichar sc);
  static PRUint8 GetDCost(PRUnichar pc);
  static PRUint8 GetSCost(PRUnichar pc, PRUnichar sc);
  static PRUnichar GetNorm(PRUnichar c);
};

class nsUnicharHashKey : public PLDHashEntryHdr
{
public:
  typedef const PRUnichar& KeyType;
  typedef const PRUnichar* KeyTypePointer;

  nsUnicharHashKey(KeyTypePointer aKey) : mValue(*aKey) { }
  nsUnicharHashKey(const nsUnicharHashKey& toCopy) : mValue(toCopy.mValue) { }
  ~nsUnicharHashKey() { }

  KeyType GetKey() const { return mValue; }
  bool KeyEquals(KeyTypePointer aKey) const { return *aKey == mValue; }

  static KeyTypePointer KeyToPointer(KeyType aKey) { return &aKey; }
  static PLDHashNumber HashKey(KeyTypePointer aKey) { return *aKey; }
  enum { ALLOW_MEMMOVE = true };

private:
  const PRUnichar mValue;
};
// why can't we simply template the entryHeader as PLDHashEntryHdr<PRUnichar>?
typedef nsDataHashtable<nsUnicharHashKey, PRInt32> UnicharMap;
typedef nsDataHashtable<nsStringHashKey, bool> DomainMap;


class DistVector {
public:
  DistVector();
  PRInt32 GetDiff(PRUnichar c);
  void SetDiff(PRUnichar c, PRInt32 val);
  //PRInt32& operator[] (PRUnichar c);  can't get a reference to nsDataHashtable!
  void Print();
  static bool IsASCII(PRUnichar c);
  static PRUint8 GetASCII(PRUnichar c);
private:
  UnicharMap mMap;
  PRInt32 mArray[128]; // fast path for ASCII chars
  static const PRUnichar NOT_ASCII;
};


/**
 * A namespace-class which contains utilities that are used by
 * nsXSSFilter. I guess this could be refactored into a namespace, but
 * right now it needs to be a class because it is an
 * implementation of an interface used for tests.
 */
class nsXSSUtils {

public:
  /**
   * Find p in a substring of s (trimming s is free). Returns the
   * matched interval on s.
   */
  static nsresult P1FastMatch(const nsAString& p, const nsAString& s,
                              double distThreshold, MatchRes& mres);
  /**
   * Find p in a substring of s (trimming s is free). Returns the
   * matched interval on p instead of s.
   */
  static nsresult P1FastMatchReverse(const nsAString& s, const nsAString& p,
                                     double distThreshold, MatchRes &mres);
  /**
   * Returns whether the current parameter represents an XSS attack
   * for the script
   */
  static bool FindInlineXSS(const nsAString& aParam,
                              const nsAString& aScript);
  /**
   * Returns whether the current parameter represents an XSS attack
   * for the url
   */
  static bool FindExternalXSS(const nsAString& aParam, nsIURI *aUri);
  /**
   * Returns the registered domain name. Handles 2nd-level tlds such
   * as google.co.uk
   */
  static nsresult GetDomain(nsIURI* aUri, nsAString& result);
  /**
   *  Main interface for XSSFilter. These two function detect which
   *  params contribute to an injected script or url.
   */
  static nsresult CheckInline(const nsAString& script, ParameterArray& aParams);
  static nsresult CheckExternal(nsIURI *aUri, nsIURI *pageURI,
                                ParameterArray& aParams);
  /**
   * Unescapes the string repeatedly until it does not change. Does
   * URL and HTML entities escaping (the latter only if a doc is
   * provided, because it needs access to DOM functionalities).
   */
  static nsresult UnescapeLoop(const nsAString& aString, nsAString& result,
                               nsIDocument *doc);
  /**
   * Escapes the HTML entities in aString and write the output to result.
   */
  static nsresult DecodeHTMLEntities(const nsAString& aString,
                                     nsAString& result,
                                     nsIDocument* doc);
  /**
   * Returns the last position where the attacker can control the domain.
   */
  static PRUint32 GetHostLimit(nsIURI* aUri);
  /**
   * Trims the input string to the biggest substring that has
   * suspicious chars. If no suspicious chars are found, it is empty.
   * The flags LEAVE_BEG and LEAVE_END can be used to disable
   * trimming.
   */
  static nsresult TrimSafeChars(const nsAString& path, nsAString& result,
                                const char *chars, PRUint8 flags = 0);
#define LEAVE_BEG 1
#define LEAVE_END 2
  /**
   * Extracts suspicious content from the URL for XSS checks,
   * appending it to aParams.
   */
  static nsresult ParseURI(nsIURI *aURI, ParameterArray& aParams,
                           nsIDocument* doc);
  /**
   * Extracts suspicious content from the post body for XSS checks,
   * appending it to aParams.
   */
  static nsresult ParsePOST(char *request, ParameterArray& aParams,
                            nsIDocument* doc);
  /**
   * Returns true if the string contains anything other than
   * [a-zA-Z0-9 _-=&]
   */
  static bool IsQueryDangerous(const nsAString& aString);
  /**
   * Checks whether at least one of the parameters in the array
   * represents an XSS attack.
   */
  static bool HasAttack(ParameterArray& aParams);
  /**
   * Initialize the logger and other static variables.
   */
  static void InitializeStatics();
};


#define NS_XSSUTILS_CID \
  { 0x1db76755, 0x1e01, 0x45fe, { 0x92, 0xb5, 0x25, 0x67, 0x9c, 0xbe, 0xb0, 0x85}}
#define NS_XSSUTILS_CONTRACTID "@mozilla.com/xssutils;1"

/**
 * This wrapper is used only for testing.
 */
class nsXSSUtilsWrapper : public nsIXSSUtils {

  NS_DECL_ISUPPORTS
  NS_DECL_NSIXSSUTILS

  static nsXSSUtilsWrapper* GetSingleton();
  static nsXSSUtilsWrapper* gXSSUtilsWrapper;

  // TODO: this was not here before, but now it generates a warning about having a non-virtual destructor
  // Does it need an implementation?
  virtual ~nsXSSUtilsWrapper() { };
};

#endif // nsXSSUtils_h
