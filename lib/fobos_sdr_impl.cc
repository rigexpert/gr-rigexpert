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
//  2024.04.26
//==============================================================================
#include <math.h>
#include "fobos_sdr_impl.h"
#include <gnuradio/io_signature.h>

namespace gr
{
    namespace RigExpert
    {
        //======================================================================
        using output_type = gr_complex;
        fobos_sdr::sptr fobos_sdr::make(int index, 
                                        double frequency_mhz, 
                                        double samplerate_mhz,
                                        int lna_gain,
                                        int vga_gain,
                                        int direct_sampling,
                                        int clock_source)
        {
            printf("make (%d, %f, %f, %d, %d, %d, %d)\n", index, frequency_mhz, samplerate_mhz, lna_gain, vga_gain, direct_sampling, clock_source);
            return gnuradio::make_block_sptr<fobos_sdr_impl>(
                                        index, 
                                        frequency_mhz, 
                                        samplerate_mhz,
                                        lna_gain,
                                        vga_gain,
                                        direct_sampling,
                                        clock_source);
        }
        //======================================================================
        // The private constructor
        fobos_sdr_impl::fobos_sdr_impl( int index, 
                                        double frequency_mhz, 
                                        double samplerate_mhz,
                                        int lna_gain,
                                        int vga_gain,
                                        int direct_sampling,
                                        int clock_source)
            : gr::sync_block("fobos_sdr",
                             gr::io_signature::make(0, 0, 0),
                             gr::io_signature::make(
                                 1, 1, sizeof(output_type)))
        {
            _rx_bufs = 0;
            _rx_idx_w = 0;
            _rx_pos_r = 0;
            _rx_idx_r = 0;
            _rx_pos_w = 0;
            _rx_filled = 0;
            _running = false;
            _buff_counter = 0;
            _overruns_count = 0;
            int count = fobos_rx_get_device_count();
            printf("fobos_sdr_impl:: found devices: %d\n", count);
            if (count > 0)
            {
                int result = 0;

                result = fobos_rx_open(&_dev, index);

                if (result == 0)
                {
                    printf("open...ok\n");
                    printf("(%d, %f, %f, %d, %d, %d, %d)\n", index, frequency_mhz, samplerate_mhz, lna_gain, vga_gain, direct_sampling, clock_source);

                    result = fobos_rx_set_frequency(_dev, frequency_mhz * 1E6, 0);
                    if (result != 0)
                    {
                        printf("fobos_rx_set_frequency - error!\n");
                    }

                    result = fobos_rx_set_samplerate(_dev, samplerate_mhz * 1E6, 0);
                    if (result != 0)
                    {
                        printf("fobos_rx_set_samplerate - error!\n");
                    }

                    result = fobos_rx_set_lna_gain(_dev, lna_gain);
                    if (result != 0)
                    {
                        printf("fobos_rx_set_lna_gain - error!\n");
                    }

                    result = fobos_rx_set_vga_gain(_dev, vga_gain);
                    if (result != 0)
                    {
                        printf("fobos_rx_set_vga_gain - error!\n");
                    }

                    result = fobos_rx_set_direct_sampling(_dev, direct_sampling);
                    if (result != 0)
                    {
                        printf("fobos_rx_set_direct_sampling - error!\n");
                    }
                    
                    result = fobos_rx_set_clk_source(_dev, clock_source);
                    if (result != 0)
                    {
                        printf("fobos_rx_set_clk_source - error!\n");
                    }

                    _rx_buffs_count = 32;
                    _rx_buff_len = 65536*2;

                    _rx_bufs = (float**)malloc(_rx_buffs_count * sizeof(float*));
                    for (unsigned int i = 0; i < _rx_buffs_count; i++)
                    {
                        _rx_bufs[i] = (float*)malloc(_rx_buff_len * 2 * sizeof(float));
                    }

                    _running = true;
                    _thread = gr::thread::thread(thread_proc, this);
                }
                else
                {
                    printf("could not open device! err (%i)\n", result);
                }
            }
            else
            {
                printf("could not find any fobos_sdr compatible device!\n");
            }
        }
        //======================================================================
        // virtual destructor
        fobos_sdr_impl::~fobos_sdr_impl()
        {
            if (_dev)
            {
                fobos_rx_cancel_async(_dev);
                fobos_rx_close(_dev);
            }
            //if (_thread.joinable()) // - does not work in boost::thread 
            if (_running)
            {
                _thread.join();
            }
            if (_rx_bufs)
            {
                for (unsigned int i = 0; i < _rx_buffs_count; i++)
                {
                    if (_rx_bufs[i])
                    {
                        free(_rx_bufs[i]);
                    }
                }
                free(_rx_bufs);
            }
        }
        //======================================================================
        // Work
        int fobos_sdr_impl::work(int noutput_items,
                                 gr_vector_const_void_star& input_items,
                                 gr_vector_void_star& output_items)
        {
            auto out = static_cast<output_type*>(output_items[0]);
            if (!_running)
            {
                printf("%d ", noutput_items);
                printf("^");
                return 0;
            }
            {
                std::unique_lock<std::mutex> lock(_rx_mutex);
                if (_rx_filled == 0)
                {
                    //printf("w");
                    _rx_cond.wait(lock);
                }
            }
            if (this->_rx_filled > 0)
            {
                float * buff = _rx_bufs[_rx_idx_r] + _rx_pos_r * 2;
                size_t samples_count = (_rx_buff_len - _rx_pos_r);
                if (samples_count > (size_t)noutput_items)
                {
                    samples_count = noutput_items;
                }
                memcpy((float*)out, buff, samples_count * 2 * sizeof(float));
                _rx_pos_r += samples_count;
                if (_rx_pos_r >= _rx_buff_len)
                {
                    std::lock_guard<std::mutex> lock(_rx_mutex);
                    _rx_pos_r = 0;
                    _rx_idx_r = (_rx_idx_r + 1) % _rx_buffs_count;
                    _rx_filled--;
                }
            }
            else
            {
                printf("u");
            }
            return noutput_items;
        }
        //======================================================================
        void fobos_sdr_impl::read_samples_callback(float *buf, uint32_t buf_length, void *ctx)
        {
            fobos_sdr_impl* _this = static_cast<fobos_sdr_impl*>(ctx);
            //printf("+");
            if (_this->_rx_buff_len != buf_length)
            {
                printf("Err: wrong buf_length!!!");
                printf("canceling...");
                fobos_rx_cancel_async(_this->_dev);
            }
            std::lock_guard<std::mutex> lock(_this->_rx_mutex);
            if (_this->_rx_filled < _this->_rx_buffs_count)
            {
                memcpy(_this->_rx_bufs[_this->_rx_idx_w], buf, _this->_rx_buff_len * 2 *sizeof(float));
                _this->_rx_idx_w = (_this->_rx_idx_w + 1) % _this->_rx_buffs_count;
                _this->_rx_filled++;
            }
            else
            {
                _this->_overruns_count++;
                // printf("#");
                //printf("OVERRUN!!!");
            }
            _this->_rx_cond.notify_one();
        }
        //======================================================================
        void fobos_sdr_impl::thread_proc(fobos_sdr_impl * _this)
        {
            int result = fobos_rx_read_async(_this->_dev, read_samples_callback, _this, 16, _this->_rx_buff_len);
            if (result == 0)
            {
                printf("fobos_rx_read_async - ok!\n");
            }
            else
            {
                printf("fobos_rx_read_async - error!\n");
            }
            _this->_running = false;
        }
        //======================================================================
        void fobos_sdr_impl::set_frequency(double frequency_mhz)
        {
            double actual;
            int res = fobos_rx_set_frequency(_dev, frequency_mhz * 1e6, &actual);
            printf("Setting freq %f MHz, actual %f MHz: %s\n", frequency_mhz, actual / 1E6, res == 0 ? "OK" : "ERR");
        }
        //======================================================================
        void fobos_sdr_impl::set_samplerate(double samplerate_mhz)
        {
            double actual;
            int res = fobos_rx_set_samplerate(_dev, samplerate_mhz * 1e6, &actual);
            printf("Setting sample rate %f MHz, actual %f MHz: %s\n", samplerate_mhz, actual / 1E6, res == 0 ? "OK" : "ERR");
        }
        //======================================================================
        void fobos_sdr_impl::set_lna_gain(int lna_gain)
        {
            int res = fobos_rx_set_lna_gain(_dev, lna_gain);
            printf("Setting LNA gain to #%d: %s\n", lna_gain, res == 0 ? "OK" : "ERR");
        }
        //======================================================================
        void fobos_sdr_impl::set_vga_gain(int vga_gain)
        {
            int res = fobos_rx_set_vga_gain(_dev, vga_gain);
            printf("Setting VGA gain to #%d: %s\n",  vga_gain, res == 0 ? "OK" : "ERR");
        }
        //======================================================================
        void fobos_sdr_impl::set_direct_sampling(int direct_sampling)
        {
            int res = fobos_rx_set_direct_sampling(_dev, direct_sampling);
            printf("Setting direct sampling mode to %d: %s\n",  direct_sampling, res == 0 ? "OK" : "ERR");
        }
        //======================================================================
        void fobos_sdr_impl::set_clock_source(int clock_source)
        {
            int res = fobos_rx_set_clk_source(_dev, clock_source);
            printf("Setting clock source to %s: %s\n",  clock_source == 0 ? "internal" : "external", res == 0 ? "OK" : "ERR");
        }
        //======================================================================
    } /* namespace RigExpert */
} /* namespace gr */
