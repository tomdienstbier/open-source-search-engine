#ifndef GB_URLMATCH_H_
#define GB_URLMATCH_H_

#include <string>
#include <memory>
#include <vector>
#include "GbRegex.h"
#include "UrlParser.h"

enum urlmatchtype_t {
	url_match_domain,
	url_match_file,
	url_match_host,
	url_match_hostsuffix,
	url_match_path,
	url_match_pathparam,
	url_match_pathpartial,
	url_match_queryparam,
	url_match_regex
};

struct urlmatchstr_t {
	urlmatchstr_t(urlmatchtype_t type, const std::string &str);

	urlmatchtype_t m_type;
	std::string m_str;
};

struct urlmatchdomain_t {
	enum pathcriteria_t {
		pathcriteria_allow_all,
		pathcriteria_allow_index_only,
		pathcriteria_allow_rootpages_only
	};

	urlmatchdomain_t(const std::string &domain, const std::string &allow, pathcriteria_t pathcriteria);

	std::string m_domain;
	std::vector<std::string> m_allow;
	pathcriteria_t m_pathcriteria;
};

struct urlmatchhost_t {
	urlmatchhost_t(const std::string &host, const std::string &path);

	std::string m_host;
	std::string m_path;
	int m_port;
};

struct urlmatchparam_t {
	urlmatchparam_t(urlmatchtype_t type, const std::string &name, const std::string &value);

	urlmatchtype_t m_type;
	std::string m_name;
	std::string m_value;
};

struct urlmatchregex_t {
	urlmatchregex_t(const std::string &regexStr, const GbRegex &regex);

	GbRegex m_regex;
	std::string m_regexStr;
};

class Url;

class UrlMatch {
public:
	UrlMatch(const std::shared_ptr<urlmatchstr_t> &urlmatchstr, bool m_invert);
	UrlMatch(const std::shared_ptr<urlmatchdomain_t> &urlmatchdomain, bool m_invert);
	UrlMatch(const std::shared_ptr<urlmatchhost_t> &urlmatchhost, bool m_invert);
	UrlMatch(const std::shared_ptr<urlmatchparam_t> &urlmatchparam, bool m_invert);
	UrlMatch(const std::shared_ptr<urlmatchregex_t> &urlmatchregex, bool m_invert);

	bool match(const Url &url, const UrlParser &urlParser) const;
	void logMatch(const Url &url) const;

private:
	bool m_invert;

	urlmatchtype_t m_type;

	std::shared_ptr<urlmatchstr_t> m_str;

	std::shared_ptr<urlmatchdomain_t> m_domain;
	std::shared_ptr<urlmatchhost_t> m_host;
	std::shared_ptr<urlmatchparam_t> m_param;
	std::shared_ptr<urlmatchregex_t> m_regex;
};

#endif //GB_URLMATCH_H_
