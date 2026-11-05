# Imported HDR reference notes

The `hdr*.md` documents and `hdr-post-licenses.txt` in this directory were
imported from [hartapfel/pt-pc-hdr](https://github.com/hartapfel/pt-pc-hdr/tree/dd90825c060722b9c0cbc90fe5f79ad7c302550e)
at `dd90825c060722b9c0cbc90fe5f79ad7c302550e` (1.0.8.3-hdr).
They describe that fork's implementation, history, Windows builds and tests.
Those historical results are not validation of this Mac integration.

See [the dual-pipeline integration report](../integration/dual-rendering-pipelines.md)
for the implementation boundaries, platform support and tests of this branch.
The Original shader family remains in the root `shaders` directory. Adapted
Enhanced shaders live in `shaders/enhanced` and are compiled separately on Mac.

The upstream project is MIT licensed. The additional perceptual grain and RCAS
notices are preserved in `hdr-post-licenses.txt` and in Mac app bundles as
`Contents/Resources/licenses/HDR-post-processing-MIT.txt`.
