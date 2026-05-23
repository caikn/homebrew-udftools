class UdftoolsMac < Formula
  desc "UDF filesystem utilities for macOS (mkudffs, udfinfo, udflabel)"
  homepage "https://github.com/caikn/homebrew-udftools"
  url "https://github.com/caikn/homebrew-udftools/archive/refs/tags/v2.3.tar.gz"
  sha256 "d546a05bbc57ae98777aa3b614e2f283cb14e79c4e0d06a8f92ceb08a3d5e9fe"
  license "GPL-2.0-only"

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
