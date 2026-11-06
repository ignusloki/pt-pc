// Synthetic sine-wave fixtures; no game audio or external encoder needed.
#include <vorbis/vorbisenc.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 5) return 2;
    const int rate = std::atoi(argv[2]), channels = std::atoi(argv[3]), frames = std::atoi(argv[4]);
    FILE* out = std::fopen(argv[1], "wb");
    if (!out) return 3;
    vorbis_info info;
    vorbis_info_init(&info);
    if (vorbis_encode_init_vbr(&info, channels, rate, 0.6f)) return 4;
    vorbis_comment comment;
    vorbis_comment_init(&comment);
    vorbis_dsp_state dsp;
    vorbis_analysis_init(&dsp, &info);
    vorbis_block block;
    vorbis_block_init(&dsp, &block);
    ogg_stream_state stream;
    ogg_stream_init(&stream, 42);
    ogg_packet headers[3];
    vorbis_analysis_headerout(&dsp, &comment, &headers[0], &headers[1], &headers[2]);
    for (auto& h : headers) ogg_stream_packetin(&stream, &h);
    ogg_page page;
    auto write = [&] { std::fwrite(page.header, 1, page.header_len, out); std::fwrite(page.body, 1, page.body_len, out); };
    while (ogg_stream_flush(&stream, &page)) write();
    float** pcm = vorbis_analysis_buffer(&dsp, frames);
    for (int c = 0; c < channels; ++c)
        for (int f = 0; f < frames; ++f)
            pcm[c][f] = static_cast<float>(12000.0 / 32768.0 * std::sin(2 * 3.141592653589793 * (220 + c * 110) * f / rate));
    vorbis_analysis_wrote(&dsp, frames);
    vorbis_analysis_wrote(&dsp, 0);
    while (vorbis_analysis_blockout(&dsp, &block) == 1) {
        vorbis_analysis(&block, nullptr);
        vorbis_bitrate_addblock(&block);
        ogg_packet packet;
        while (vorbis_bitrate_flushpacket(&dsp, &packet)) {
            ogg_stream_packetin(&stream, &packet);
            while (ogg_stream_pageout(&stream, &page)) write();
        }
    }
    const bool failed = std::ferror(out);
    std::fclose(out);
    ogg_stream_clear(&stream);
    vorbis_block_clear(&block);
    vorbis_dsp_clear(&dsp);
    vorbis_comment_clear(&comment);
    vorbis_info_clear(&info);
    return failed;
}
