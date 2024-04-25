//==============================================================================
//       _____     __           _______
//      /  __  \  /_/          /  ____/                                __
//     /  /_ / / _   ____     / /__  __  __   ____    ____    ____   _/ /_
//    /    __ / / / /  _  \  / ___/  \ \/ /  / __ \  / __ \  / ___\ /  _/
//   /  /\ \   / / /  /_/ / / /___   /   /  / /_/ / /  ___/ / /     / /_
//  /_ /  \_\ /_/  \__   / /______/ /_/\_\ / ____/  \____/ /_/      \___/
//               /______/                 /_/             
//  Fobos SDR API library
//  Copyright (C) Rig Expert Ukraine Ltd.
//  2024.03.21
//  2024.04.08
//  2024.04.21
//==============================================================================

#ifndef INCLUDED_RIGEXPERT_FOBOS_SDR_IMPL_H
#define INCLUDED_RIGEXPERT_FOBOS_SDR_IMPL_H

#include <gnuradio/sync_block.h>
#include <gnuradio/thread/thread.h>
#include <mutex>
#include <condition_variable>
#include <gnuradio/RigExpert/fobos_sdr.h>
#include <fobos/fobos.h>

namespace gr
{
    namespace RigExpert
    {
        class fobos_sdr_impl : public fobos_sdr
        {
        private:
            uint32_t _buff_counter;
            bool _running;
            gr::thread::thread _thread;
            std::mutex _rx_mutex;
            std::condition_variable _rx_cond;
            float ** _rx_bufs;
            size_t _rx_buffs_count;
            size_t _rx_buff_len;
            size_t _rx_filled;
            size_t _rx_idx_w;
            size_t _rx_pos_w;
            size_t _rx_idx_r;
            size_t _rx_pos_r;
            uint32_t _overruns_count;
            struct fobos_dev_t * _dev = NULL;
            static void read_samples_callback(float * buf, uint32_t buf_length, void * ctx);
            static void thread_proc(fobos_sdr_impl * ctx);
        public:
            fobos_sdr_impl( int index, 
                            double frequency_mhz, 
                            double samplerate_mhz,
                            int lna_gain,
                            int vga_gain,
                            int direct_sampling,
                            int clock_source);
            ~fobos_sdr_impl();

            int work(int noutput_items,
                     gr_vector_const_void_star& input_items,
                     gr_vector_void_star& output_items);

            void set_frequency(double freq);
            void set_lna_gain(int lna_g);
            void set_vga_gain(int vga_g);
        };

    } // namespace RigExpert
} // namespace gr

#endif /* INCLUDED_RIGEXPERT_FOBOS_SDR_IMPL_H */
