#!/usr/bin/env ruby

require 'net/http'
require 'net/https'
require 'uri'

# The fetch function was based off of the function at the following URL
# http://stackoverflow.com/questions/6934185/ruby-net-http-following-redirects

# This will only follow 302 redirects currently
# This script came to fruition after @digininja requesting this feature be added to EyeWitness
# Thanks to @digininja for the suggestion and great idea.  This has been added in to EyeWitness too.

def fetch(uri_str, url_list, limit = 10)
  # This checks up to 10 redirects.  If it keeps going further, change the limit value
  raise ArgumentError, 'HTTP redirect too deep' if limit == 0

  uri = URI.parse(uri_str)

  if uri_str.start_with?("http://")
    # code came from - http://www.rubyinside.com/nethttp-cheat-sheet-2940.html
    http = Net::HTTP.new(uri.host, uri.port)
    request = Net::HTTP::Get.new(uri.request_uri)
  elsif uri_str.start_with?("https://")
    http = Net::HTTP.new(uri.host, uri.port)
    http.use_ssl = true
    http.verify_mode = OpenSSL::SSL::VERIFY_NONE
    request = Net::HTTP::Get.new(uri.request_uri)
  end

  response = http.request(request)
  case response
  when Net::HTTPSuccess
    url_list.push("#{uri_str} <- Final URL")
  when Net::HTTPRedirection
    url_list.push("#{uri_str} redirects to...")
    uri = URI.parse(uri_str)
    base_url = "#{uri.scheme}://#{uri.host}"
    new_url = URI.parse(response.header['location'])
    if (new_url.relative?)
      new_url = base_url + response.header['location']
      fetch(new_url, url_list, limit - 1)
    else
      fetch(response['location'], url_list, limit - 1)
    end
  else
    response.error!
  end
end

# Check to make sure we have only one argument, the URL
if ARGV.length != 1
  puts "[*] Error: Please provide a URL to check for redirects!"
  puts "[*] Usage: webtrace <URL>"
  exit
end

# Check to make sure it's a valid URL
if ARGV[0] =~ URI::regexp
else
  puts "[*] Error: Please provide a valid URL!"
  puts "[*] Usage: webtrace <URL>"
  exit
end

# Array which will store all redirects
all_urls = []

# Function that checks for redirects
fetch(ARGV[0], all_urls)

# If no redirects, say so.  Otherwise, list all redirects
if all_urls.length == 1
  puts "No Redirection"
else
  all_urls.each do |ind_url|
    puts ind_url
  end
end
