#!/usr/bin/env ruby
# frozen_string_literal: true

require "digest"
require "fileutils"
require "json"

command = File.basename($PROGRAM_NAME)
File.open(ENV.fetch("COMMAND_LOG"), "a") { |log| log.puts(JSON.generate([command, *ARGV])) }
key = [command, ARGV.first].join(" ")
exit 1 if ENV["FAIL_COMMAND"] == key

def github_command
  case ARGV.take(2)
  when ["release", "view"]
    puts ENV.fetch("RELEASE_METADATA", "false\tfalse\tv0.1.0")
  when ["release", "download"]
    destination = ARGV.fetch(ARGV.index("--dir") + 1)
    FileUtils.cp(Dir[File.join(ENV.fetch("ASSETS"), "*")], destination)
  when ["attestation", "verify"]
    abort "wrong source ref" unless ARGV.include?("refs/tags/v0.1.0")
    abort "missing signer" unless ARGV.include?("yowainwright/src-lint/.github/workflows/release.yml")
    abort "missing bundle" unless File.file?(ARGV.fetch(ARGV.index("--bundle") + 1))
  when ["pr", "list"]
    puts ENV.fetch("EXISTING_PR", "")
  when ["pr", "create"]
    abort "missing PR body" unless File.file?(ARGV.fetch(ARGV.index("--body-file") + 1))
    puts "https://github.com/yowainwright/homebrew-tap/pull/1"
  else abort "unexpected gh command: #{ARGV}"
  end
end

def brew_command
  installed = ENV.fetch("INSTALLED_TAP")
  case ARGV.first
  when "tap" then FileUtils.mkdir_p(File.join(installed, "Formula"))
  when "--repository" then puts installed
  when "audit", "install", "test"
    expected = File.read("Formula/src-lint.rb")
    abort "wrong installed formula" unless File.read(File.join(installed, "Formula/src-lint.rb")) == expected
  else abort "unexpected brew command: #{ARGV}"
  end
end

def generate_formula
  abort "wrong generator arguments" unless ARGV == ["src-lint", "0.1.0"]
  data = JSON.parse(File.read("brews/src-lint.json"))
  abort "inactive metadata" unless data.values_at("managed", "readme", "version") == [true, true, "0.1.0"]
  abort "missing README" unless File.read("README.md").include?("brew install yowainwright/tap/src-lint")
  assets = Dir[File.join(ENV.fetch("ASSETS"), "src-lint-*")].reject { |path| File.basename(path).include?(".") }
  checksums = assets.map { |path| "  sha256 \"#{Digest::SHA256.file(path).hexdigest}\"" }
  FileUtils.mkdir_p("Formula")
  File.write("Formula/src-lint.rb", ["class SrcLint < Formula", *checksums, "end", ""].join("\n"))
end

case command
when "gh" then github_command
when "brew" then brew_command
when "new-formula", "update-formula" then generate_formula
when "validate-tap" then exit 0
when "git"
  puts "dirty" if ARGV.first == "status" && ENV["DIRTY_TAP"] == "1"
  puts "abc refs/heads/src-lint-v0.1.0" if ARGV.first == "ls-remote" && ENV["EXISTING_PR"]
  exit ENV.fetch("DIFF_STATUS", "1").to_i if ARGV.first == "diff"
else abort "unexpected command: #{command}"
end
