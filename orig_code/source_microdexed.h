
class AudioSourceMicroDexed : public AudioStream, public Dexed {
  public:
    const uint16_t audio_block_time_us = 1000000 / (SAMPLE_RATE / AUDIO_BLOCK_SAMPLES);
    uint32_t xrun = 0;
    uint16_t render_time_max = 0;

    AudioSourceMicroDexed(int sample_rate) : AudioStream(0, NULL), Dexed(sample_rate) { };

    void update(void)
    {
      if (in_update == true)
      {
        xrun++;
        return;
      }
      else
        in_update = true;

      elapsedMicros render_time;
      audio_block_t *lblock;

      lblock = allocate();

      if (!lblock)
      {
        in_update = false;
        return;
      }

      getSamples(AUDIO_BLOCK_SAMPLES, lblock->data);

      if (render_time > audio_block_time_us) // everything greater audio_block_time_us (2.9ms for buffer size of 128) is a buffer underrun!
        xrun++;

      if (render_time > render_time_max)
        render_time_max = render_time;

      transmit(lblock, 0);
      release(lblock);

      in_update = false;
    };

  private:
    volatile bool in_update = false;
};
