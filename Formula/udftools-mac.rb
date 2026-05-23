class UdftoolsMac < Formula
  desc "UDF filesystem utilities for macOS (mkudffs, udfinfo, udflabel)"
  homepage "https://github.com/pali/udftools"
  # TODO: Update URL and sha256 when published to a public repo
  url "https://github.com/user/udftools-mac/archive/refs/tags/v2.3-macos.tar.gz"
  sha256 ""
  license "GPL-2.0-only"
  version "2.3-macos"

  depends_on :macos

  def install
    system "make"
    system "make", "install", "PREFIX=#{prefix}"
  end

  test do
    system "#{bin}/mkudffs", "--help"
    system "#{bin}/udfinfo", "--help"
    system "#{bin}/udflabel", "--help"
  end
end
