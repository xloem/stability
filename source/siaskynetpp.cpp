#include <siaskynet.hpp>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include <iostream>

namespace sia {

static std::string writeToField(std::vector<skynet::upload> const & files, std::string const & filename, std::string const & url, std::string const & field);
static std::string trimSiaPrefix(std::string const & skylink);
static std::string trimTrailingSlash(std::string const & url);
static skynet::response::subfile parseCprResponse(cpr::Response & response);
static std::string extractContentDispositionFilename(std::string const & content_disposition);
static skynet::response::subfile parse_subfile(nlohmann::json const & value);

skynet::portal_options skynet::default_options = {
	url: "https://siasky.net",
	uploadPath: "/skynet/skyfile",
	fileFieldname: "file",
	directoryFileFieldname: "files[]",
};

skynet::skynet(skynet::portal_options const & options)
: options(options)
{ }

std::string trimSiaPrefix(std::string const & skylink)
{
	if (0 == skylink.compare(0, 6, "sia://")) {
		return skylink.substr(6);
	} else {
		return skylink;
	}
}

std::string trimTrailingSlash(std::string const & url)
{
	if (url[url.size() - 1] == '/') {
		std::string result = url;
		result.resize(result.size() - 1);
		return result;
	} else {
		return url;
	}
}

/*
std::string skynet::write(std::string const & data, std::string const & filename)
{
	std::vector<uint8_t> bytes(data.begin(), data.end());
	return write(bytes, filename);
}
*/

std::string skynet::write(std::vector<uint8_t> const & data, std::string const & filename)
{
	auto url = cpr::Url{trimTrailingSlash(options.url) + trimTrailingSlash(options.uploadPath)};

	return writeToField({upload{filename, data}}, filename, url, options.fileFieldname);
}

std::string skynet::write(std::vector<skynet::upload> const & files, std::string const & filename)
{
	auto url = cpr::Url{trimTrailingSlash(options.url) + "/" + trimTrailingSlash(options.uploadPath)};

	return writeToField(files, filename, url, options.directoryFileFieldname);
}

std::string writeToField(std::vector<skynet::upload> const & files, std::string const & filename, std::string const & url, std::string const & field)
{
	auto parameters = cpr::Parameters{{"filename", filename}};

	cpr::Multipart uploads{};

	for (auto & file : files) {
		uploads.parts.emplace_back(field, cpr::Buffer{file.data.begin(), file.data.end(), file.filename});
	}

	auto response = cpr::Post(url, parameters, uploads);
	auto json = nlohmann::json::parse(response.text);

	std::string skylink = "sia://" + json["skylink"].get<std::string>();

	return skylink;
}

skynet::response skynet::query(std::string const & skylink)
{
	std::string url = trimTrailingSlash(options.url) + "/" + trimSiaPrefix(skylink);

	auto response = cpr::Head(url);

	skynet::response result;
	result.skylink = skylink;
	result.portal = options;
	result.filename = extractContentDispositionFilename(response.header["content-disposition"]);
	result.metadata = parseCprResponse(response);

	return result;
}

skynet::response skynet::read(std::string const & skylink)
{
	std::string url = trimTrailingSlash(options.url) + "/" + trimSiaPrefix(skylink);

	auto response = cpr::Get(url, cpr::Parameters{{"format","concat"}});
	if (response.status_code != 200) {
		throw std::runtime_error(response.text);
	}

	skynet::response result;
	result.skylink = skylink;
	result.portal = options;
	result.filename = extractContentDispositionFilename(response.header["content-disposition"]);
	result.metadata = parseCprResponse(response);
	result.data = std::vector<uint8_t>(response.text.begin(), response.text.end());

	return result;
}

skynet::response::subfile parse_subfile(nlohmann::json const & value)
{
	skynet::response::subfile metadata;
	metadata.contenttype = value["contenttype"].get<std::string>();
	//metadata.len = std::stoul(value["len"].get<std::string>());
	metadata.len = value["len"].get<size_t>();
	metadata.filename = value["filename"].get<std::string>();
	if (!value.contains("subfiles")) { return metadata; }

	for (auto & subfile : value["subfiles"].items()) {
		metadata.subfiles.emplace_back(subfile.key(), parse_subfile(subfile.value()));
	}
	return metadata;
}

skynet::response::subfile parseCprResponse(cpr::Response & cpr)
{
	auto & raw_json = cpr.header["skynet-file-metadata"];
	auto parsed_json = nlohmann::json::parse(raw_json);
	parsed_json["len"] = std::stoul(cpr.header["content-length"]);
	parsed_json["contenttype"] = cpr.header["content-type"];

	return parse_subfile(parsed_json);
}

std::string extractContentDispositionFilename(std::string const & content_disposition)
{
	auto start = content_disposition.find("filename=");
	if (start == std::string::npos) { return {}; }

	start += 9;
	auto first_char = content_disposition[start];
	auto last_char = ';';
	if (first_char == '\'' || first_char == '"') {
		++ start;
		last_char = first_char;
	}
	
	auto end = content_disposition.find(last_char, start);
	if (end == std::string::npos) {
		end = content_disposition.size();
	}

	return { content_disposition.begin() + start, content_disposition.begin() + end };
}

}
