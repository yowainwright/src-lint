# frozen_string_literal: true

require "digest"
require "fileutils"
require "json"
require "open3"
require "tmpdir"

SOURCE, WORK_ROOT, CLI, VERSION, SCENARIO, DETAIL = ARGV
TARGETS = %w[darwin-arm64 darwin-amd64 linux-arm64 linux-amd64].freeze
FileUtils.mkdir_p(WORK_ROOT)

def check(condition, message)
  abort "release test: #{message}" unless condition
end

def write_executable(path, content)
  File.write(path, content)
  File.chmod(0o755, path)
end

def write_assets(root)
  TARGETS.each do |target|
    name = "src-lint-#{target}"
    path = File.join(root, "assets", name)
    write_executable(path, "#!/bin/sh\n# #{target}\nprintf 'src-lint 0.1.0\\n'\n")
    File.write("#{path}.sha256", "#{Digest::SHA256.file(path).hexdigest}  #{name}\n")
    File.write("#{path}.sigstore.json", "{}\n")
  end
end

def setup_fixture(root)
  %w[tap/scripts tap/brews bin assets].each { |path| FileUtils.mkdir_p(File.join(root, path)) }
  FileUtils.cp(File.join(SOURCE, "tests/fixtures/release/src-lint.json"), File.join(root, "tap/brews/src-lint.json"))
  File.write(File.join(root, "tap/README.md"), "# Tap\n<!-- formulas:start -->\n### existing\nKeep this.\n\n---\n<!-- formulas:end -->\n")
  stub = File.read(File.join(SOURCE, "tests/integration/scripts/release_command_stub.rb"))
  %w[git gh brew].each { |name| write_executable(File.join(root, "bin", name), stub) }
  %w[new-formula update-formula validate-tap].each do |name|
    write_executable(File.join(root, "tap/scripts", name), stub)
  end
  write_assets(root)
end

def fixture
  Dir.mktmpdir("release-", WORK_ROOT) do |root|
    setup_fixture(root)
    yield root
  end
end

def run_release(root, overrides = {}, tag = "v0.1.0")
  # Fixtures use standard libraries only; skip RubyGems startup for each mocked command.
  ruby_options = [ENV["RUBYOPT"], "--disable-gems"].compact.join(" ")
  environment = { "PATH" => "#{root}/bin:#{ENV.fetch("PATH")}", "GH_TOKEN" => "test",
                  "RUBYOPT" => ruby_options,
                  "COMMAND_LOG" => "#{root}/commands.jsonl", "ASSETS" => "#{root}/assets",
                  "INSTALLED_TAP" => "#{root}/installed-tap" }.merge(overrides)
  output, status = Open3.capture2e(environment, "bash", "#{SOURCE}/scripts/release.sh", "homebrew-pr", "#{root}/tap", tag)
  commands = File.readlines("#{root}/commands.jsonl").map { |line| JSON.parse(line) } if File.exist?("#{root}/commands.jsonl")
  [status.success?, commands || [], output]
end

def called?(commands, *prefix)
  commands.any? { |command| command.take(prefix.size) == prefix }
end

def assert_stopped(root, overrides = {}, tag = "v0.1.0")
  success, commands, output = run_release(root, overrides, tag)
  check(!success, "accepted failure #{overrides}: #{output}")
  %w[commit push].each { |command| check(!called?(commands, "git", command), "published after failure") }
  check(!called?(commands, "gh", "pr", "create"), "opened PR after failure")
end

def test_initial_release
  fixture do |root|
    success, commands, output = run_release(root)
    check(success, output)
    check(called?(commands, "new-formula"), "did not create formula")
    check(called?(commands, "gh", "pr", "create"), "did not create PR")
    check(called?(commands, "brew", "test"), "did not test installed formula")
    check(commands.count { |command| command.take(2) == %w[gh attestation] } == 4, "did not verify every architecture")
    readme = File.read("#{root}/tap/README.md")
    check(readme.include?("Keep this."), "lost unrelated README section")
    check(Dir["#{root}/tap/.src-lint-release.*"].empty?, "left release downloads behind")
  end
end

def test_retry
  fixture do |root|
    check(run_release(root).first, "initial release failed")
    File.write("#{root}/commands.jsonl", "")
    success, commands, output = run_release(root, "EXISTING_PR" => "https://github.com/yowainwright/homebrew-tap/pull/1", "DIFF_STATUS" => "0")
    check(success, output)
    check(called?(commands, "update-formula"), "retry did not update formula")
    check(called?(commands, "git", "fetch"), "retry did not reuse branch")
    check(!called?(commands, "git", "commit"), "committed unchanged formula")
    check(!called?(commands, "gh", "pr", "create"), "retry duplicated PR")
    check(File.read("#{root}/tap/README.md").scan("### [src-lint]").size == 1, "retry duplicated README section")
  end
end

def test_command_failure(command)
  fixture do |root|
    success, commands, output = run_release(root, "FAIL_COMMAND" => command)
    check(!success, "accepted #{command} failure: #{output}")
    check(!called?(commands, "gh", "pr", "create"), "opened PR after #{command} failure")
    next if command.start_with?("git ")

    check(!called?(commands, "git", "commit"), "committed after #{command} failure")
    check(!called?(commands, "git", "push"), "pushed after #{command} failure")
  end
end

def test_precondition_failure(condition)
  overrides = case condition
              when "missing_token" then { "GH_TOKEN" => "" }
              when "dirty_tap" then { "DIRTY_TAP" => "1" }
              when "diff_error" then { "DIFF_STATUS" => "2" }
              when "draft" then { "RELEASE_METADATA" => "true\tfalse\tv0.1.0" }
              when "prerelease" then { "RELEASE_METADATA" => "false\ttrue\tv0.1.0" }
              when "wrong_version" then { "RELEASE_METADATA" => "false\tfalse\tv0.2.0" }
              else abort "unknown precondition: #{condition}"
              end
  fixture { |root| assert_stopped(root, overrides) }
end

def test_missing_asset(target)
  check(TARGETS.include?(target), "unknown target: #{target}")
  fixture do |root|
    File.delete("#{root}/assets/src-lint-#{target}")
    assert_stopped(root)
    check(JSON.parse(File.read("#{root}/tap/brews/src-lint.json"))["managed"] == false, "activated incomplete release")
  end
end

def test_bad_checksum
  fixture do |root|
    File.write("#{root}/assets/src-lint-linux-arm64.sha256", "wrong\n")
    assert_stopped(root)
  end
end

def test_missing_attestation
  fixture do |root|
    File.delete("#{root}/assets/src-lint-darwin-amd64.sigstore.json")
    assert_stopped(root)
  end
end

def test_missing_readme
  fixture do |root|
    File.write("#{root}/tap/README.md", "no formula markers\n")
    assert_stopped(root)
    check(JSON.parse(File.read("#{root}/tap/brews/src-lint.json"))["managed"] == false, "changed metadata after README failure")
  end
end

def test_wrong_repository
  fixture do |root|
    path = "#{root}/tap/brews/src-lint.json"
    File.write(path, File.read(path).sub("yowainwright/src-lint", "unexpected/project"))
    assert_stopped(root)
  end
end

def test_invalid_tag(tag)
  fixture { |root| assert_stopped(root, {}, tag) }
end

def test_versions
  output, status = Open3.capture2e("bash", "#{SOURCE}/scripts/release.sh", "verify-version", CLI, "v#{VERSION}")
  check(status.success?, output)
  _, status = Open3.capture2e("bash", "#{SOURCE}/scripts/release.sh", "verify-version", CLI, "v99.0.0")
  check(!status.success?, "accepted binary version mismatch")
end

case SCENARIO
when "initial" then test_initial_release
when "retry" then test_retry
when "command_failure" then test_command_failure(DETAIL)
when "precondition_failure" then test_precondition_failure(DETAIL)
when "missing_asset" then test_missing_asset(DETAIL)
when "bad_checksum" then test_bad_checksum
when "missing_attestation" then test_missing_attestation
when "missing_readme" then test_missing_readme
when "wrong_repository" then test_wrong_repository
when "invalid_tag" then test_invalid_tag(DETAIL)
when "versions" then test_versions
else abort "unknown release scenario: #{SCENARIO.inspect}"
end
puts "release #{SCENARIO} #{DETAIL}: passed"
