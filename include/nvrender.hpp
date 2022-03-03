#ifndef _NVRENDER_H_
#define _NVRENDER_H_

#include <opencv2/opencv.hpp>
#include "NvEglRenderer.h"
#include "nvbuf_utils.h"
#include "spdlog/spdlog.h"
#include "stitcherconfig.h"

static int offsetX, offsetY, h, w;
static double fitscale;
class nvrender
{
public:
    nvrender(nvrenderCfg cfg):nvbufferWidth(cfg.bufferw), nvbufferHeight(cfg.bufferh),
    m_mode(cfg.mode)
    {
        NvBufferCreateParams bufparams = {0};
        bufparams.payloadType = NvBufferPayload_SurfArray;
        bufparams.width = cfg.bufferw;
        bufparams.height = cfg.bufferh;
        bufparams.layout = NvBufferLayout_Pitch;
        bufparams.colorFormat = NvBufferColorFormat_ARGB32;
        bufparams.nvbuf_tag = NvBufferTag_NONE;

        if (-1 == NvBufferCreateEx(&nvbufferfd, &bufparams))
                spdlog::critical("Failed to create NvBuffer nvbufferfd");
        
        if(cfg.mode == RENDER_EGL)
        {
            renderer = NvEglRenderer::createEglRenderer("renderer0", cfg.renderw, cfg.renderh, cfg.renderx, cfg.rendery);
            if(!renderer)
                spdlog::critical("Failed to create EGL renderer");
            renderer->setFPS(30);

            canvas = cv::Mat(cfg.bufferh, cfg.bufferw, CV_8UC4);
            canvas.setTo(0);

            NvBufferParams params = {0};
            NvBufferGetParams (nvbufferfd, &params);
            spdlog::debug("params num planes:{}", params.num_planes);
            if(0 != NvBufferMemMap (nvbufferfd, 0, NvBufferMem_Read_Write, (void**)&canvas.data))
                spdlog::critical("NvBufferMemMap Failed");
        }
        else if(cfg.mode == RENDER_OCV)
        {
            canvas = cv::Mat(cfg.bufferh, cfg.bufferw, CV_8UC3);
            canvas.setTo(0);
        }
        drawIndicator();
        
        spdlog::debug("cfg.bufferh:{}, cfg.bufferw:{}",cfg.bufferh, cfg.bufferw);
        spdlog::debug("render ctor cplt");
    }
    ~nvrender()
    {
        if(renderer != nullptr)
        {
            delete renderer;
            renderer = nullptr;
        }

        NvBufferDestroy(nvbufferfd);
    }

    void render(unsigned char *data)
    {
        Raw2NvBuffer(data, 0, nvbufferWidth, nvbufferHeight, nvbufferfd);
        renderer->render(nvbufferfd);
    }

    void drawIndicator()
    {
        indicatorStartX = 30;
        longStartX = 0;
        uplongStartY = 200;
        uplongEndY = uplongStartY+30;
        upshortEndY = uplongStartY+15;
        for(int i=0;i<19;i++)
        {
            longStartX = indicatorStartX+i*102;
            cv::line(canvas, cv::Point(longStartX, uplongStartY), cv::Point(longStartX, uplongEndY), cv::Scalar(0, 255, 0), 3);
            if(i==0)
                cv::putText(canvas, std::to_string(i*10), cv::Point(longStartX-10, uplongStartY-10), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
            else
                cv::putText(canvas, std::to_string(i*10), cv::Point(longStartX-20, uplongStartY-10), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
            if(i == 18)
                continue;
            for(int j=0;j<4;j++)
                cv::line(canvas, cv::Point(longStartX+20*(1+j), uplongStartY), cv::Point(longStartX+20*(1+j), upshortEndY), cv::Scalar(0, 255, 0), 3);
        }

        downlongStartY = 880;
        downlongEndY = downlongStartY+30;
        downshortEndY = downlongStartY+15;
        for(int i=0;i<19;i++)
        {
            longStartX = indicatorStartX+i*102;
            cv::line(canvas, cv::Point(longStartX, downlongStartY), cv::Point(longStartX, downlongEndY), cv::Scalar(0, 255, 0), 3);
            if(i==0)
                cv::putText(canvas, std::to_string(180+i*10), cv::Point(longStartX-10, downlongEndY+30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
            else
                cv::putText(canvas, std::to_string(180+i*10), cv::Point(longStartX-20, downlongEndY+30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
            if(i == 18)
                continue;
            for(int j=0;j<4;j++)
                cv::line(canvas, cv::Point(longStartX+20*(1+j), downlongStartY), cv::Point(longStartX+20*(1+j), downshortEndY), cv::Scalar(0, 255, 0), 3);
        }

        maxWidth = 18*102;
        maxHeight = downlongStartY - uplongEndY - 5;
    }

    void fit2final(cv::Mat &input, cv::Mat &output)
    {
        // int offsetX, offsetY, h, w; 
        static bool fitsizeok = false;
        cv::Mat tmp;
        if(!fitsizeok)
        {
            fitscale = 1;
            if(input.cols > maxWidth)
            {
                fitscale = maxWidth * 1.0 / input.cols;
            }
            cv::resize(input, tmp, cv::Size(), fitscale, fitscale);
            w = tmp.cols;
            h = tmp.rows;
            offsetX = indicatorStartX + (maxWidth - w)/2;
            offsetY = uplongEndY + (maxHeight - h)/2;

            fitsizeok = true;
        }
        cv::resize(input, tmp, cv::Size(), fitscale, fitscale);
        tmp.copyTo(output(cv::Rect(offsetX, offsetY, w, h)));
    }


    void renderegl(cv::Mat &img)
    {
        cv::Mat tmp;
        cv::cvtColor(img, tmp, cv::COLOR_RGB2RGBA);
        fit2final(tmp, canvas);
        if(0 != NvBufferMemSyncForDevice (nvbufferfd, 0, (void**)&canvas.data))
            spdlog::warn("NvBufferMemSyncForDevice failed");
        renderer->render(nvbufferfd);
        
    }

    void renderocv(cv::Mat &img, cv::Mat &final)
    {
        fit2final(img, canvas);
        cv::imshow("final", canvas);
        final  = canvas.clone();
        // cv::waitKey(1);
    }

    void render(cv::Mat &img)
    {
        std::time_t tt = chrono::system_clock::to_time_t (chrono::system_clock::now());
        // struct std::tm * ptm = std::localtime(&tt);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&tt), "%F-%H-%M-%S");
        std::string str = ss.str();
        // std::cout << "Now (local time): " << std::put_time(ptm,"%F-%H-%M-%S") << '\n';
        cv::putText(img, str, cv::Point(20, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);

        cv::Mat tmp;
        if(m_mode == RENDER_EGL)
            renderegl(img);
        else if(m_mode == RENDER_OCV)
            renderocv(img, tmp);
    }
    void render(cv::Mat &img, cv::Mat &final)
    {
        std::time_t tt = chrono::system_clock::to_time_t (chrono::system_clock::now());
        // struct std::tm * ptm = std::localtime(&tt);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&tt), "%F-%H-%M-%S");
        std::string str = ss.str();
        // std::cout << "Now (local time): " << std::put_time(ptm,"%F-%H-%M-%S") << '\n';
        cv::putText(img, str, cv::Point(20, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);

        if(m_mode == RENDER_EGL)
            renderegl(img);
        else if(m_mode == RENDER_OCV)
            renderocv(img, final);
    }

private:
    NvEglRenderer *renderer;
    int nvbufferfd;
    int nvbufferWidth, nvbufferHeight;
    cv::Mat canvas;
    int m_mode;
    int maxHeight, maxWidth;
    int longStartX, uplongStartY, uplongEndY, upshortEndY;
    int downlongStartY, downlongEndY, downshortEndY;
    int indicatorStartX;
};

#endif