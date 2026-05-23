class UdftoolsMac < Formula
  desc "UDF filesystem utilities for macOS (mkudffs, udfinfo, udflabel)"
  homepage "https://github.com/caikn/homebrew-udftools"
  url "https://github.com/caikn/homebrew-udftools/archive/refs/tags/v2.3.tar.gz"
  sha256 "cdea247d2a180f368a62e812cf22def715a8bb4df1a94ee01dcd6d923eb6f211"
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
