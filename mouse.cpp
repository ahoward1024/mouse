#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

// TODO: Get rid of all of this DLL nonsense and statically compile all dependencies
// so they are built directly into the .exe with /MT.

#include <SDL2/SDL.h>
#include <SDL2/SDL_gfxPrimitives.h>
#include <SDL2/SDL_ttf.h>

extern "C" 
{
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
	#include <libavutil/avconfig.h>
	#include <libswresample/swresample.h>
}

#include "xtrace.h" // NOTE: See note inside xtrace.h (for Visual Studio debugging)

#include "datatypes.h"
#include "colors.h"

struct Border
{
	SDL_Rect top;
	SDL_Rect bot;
	SDL_Rect left;
	SDL_Rect right;
};      

global Border currentBorder, compositeBorder,
timelineBorder, browserBorder, effectsBorder;

// Video only clips
// NOTE:IMPORTANT: Video clips MUST be initalized to zero.
struct VideoClip
{
	AVFormatContext    *formatCtx;
	AVCodecContext     *codecCtx;
	AVCodec            *codec;
	AVStream           *stream;
	AVFrame            *frame;
	AVPacket            packet;
	struct SwsContext  *swsCtx;
	int                 streamIndex;
	SDL_Texture        *texture;
	SDL_Rect            srcRect;
	SDL_Rect            destRect;
	int32               yPlaneSz;
	int32               uvPlaneSz; 
	uint8              *yPlane;
	uint8              *uPlane; 
	uint8              *vPlane;
	int32               uvPitch;
	const char         *filename;
	float               framerate;
	float               avgFramerate;
	int                 aspectRatioW;
	int                 aspectRatioH;
	float               aspectRatioF;
	int                 currentFrame;
	int                 frameCount;
	int                 width;
	int                 height;
	bool								loop;
};

struct AudioClip
{
	// TODO: Create audio only clips
};

struct AVClip
{
	// TODO: Create audio and video clips
};

enum BorderSide
{
	BORDER_SIDE_INSIDE,
	BORDER_SIDE_OUTSIDE,
};

enum WindowLayout
{
	LAYOUT_SINGLE,
	LAYOUT_DUAL,
};

global VideoClip Global_VideoClip = {};

global const char *Anime404mp4 = "../res/Anime404.mp4";
global const char *Anime404webm= "../res/Anime404.webm";
global const char *dance = "../res/dance.mp4";
global const char *froggy = "../res/froggy.gif";
global const char *groggy = "../res/groggy.gif";
global const char *h2bmkv = "../res/h2b.mkv"; // FIXME: Does not decode correctly
global const char *h2bmp4 = "../res/h2b.mp4";
global const char *kiloshelos = "../res/kiloshelos.mp4";
global const char *nggyu = "../res/nggyu.mp4";
global const char *pepsimanmvk = "../res/PEPSI-MAN.mkv";
global const char *pepsimanmp4 = "../res/PEPSI-MAN.mp4";
global const char *ruskie = "../res/ruskie.webm";
global const char *trump = "../res/trump.webm";
global const char *vapor = "../res/vapor.webm";
global const char *watamote = "../res/watamote.webm";

global WindowLayout Global_Layout = LAYOUT_SINGLE;

global bool Global_Running = true;
global bool Global_Paused = false;

global SDL_Window *window;
global SDL_Renderer *renderer;

// TODO: Make these a user setting
global int Global_AspectRatio_W = 16;
global int Global_AspectRatio_H = 9;

global bool Global_Show_Transform_Tool = false;

// TODO: List of view rectangles to pass around
global SDL_Rect currentViewBack, compositeViewBack, currentView, compositeView,
timelineView, browserView, effectsView;

// This will free the clip for reinitalization, we do not free the texture
// as SDL still needs it for video resizing.
void freeVideoClip(VideoClip *clip)
{
	av_frame_free(&clip->frame);
	avcodec_close(clip->codecCtx);
	avformat_close_input(&clip->formatCtx);
	free(clip->formatCtx);
	free(clip->yPlane);
	free(clip->uPlane);
	free(clip->vPlane);
}

// This will free the clip entirely so it CANNOT be used again.
void freeVideoClipFull(VideoClip *clip)
{
	freeVideoClip(clip);
	SDL_free(clip->texture);
}

int playVideoClip(void *data)
{
	VideoClip *clip = (VideoClip *)data;
	int frameFinished;
	if(av_read_frame(clip->formatCtx, &clip->packet) >= 0)
	{
		if(clip->packet.stream_index == clip->streamIndex)
		{
			do
			{
				avcodec_decode_video2(clip->codecCtx, clip->frame, &frameFinished, &clip->packet);
			} while(!frameFinished);

			AVPicture pict;
			pict.data[0] = clip->yPlane;
			pict.data[1] = clip->uPlane;
			pict.data[2] = clip->vPlane;
			pict.linesize[0] = clip->codecCtx->width;
			pict.linesize[1] = clip->uvPitch;
			pict.linesize[2] = clip->uvPitch;

			sws_scale(clip->swsCtx, (uint8 const * const *)clip->frame->data, 
			          clip->frame->linesize, 
			          0, clip->codecCtx->height, pict.data, pict.linesize);

			SDL_UpdateYUVTexture(clip->texture, NULL, clip->yPlane, 
			                     clip->codecCtx->width, clip->uPlane,
			                     clip->uvPitch, clip->vPlane, clip->uvPitch);

			clip->currentFrame = clip->frame->coded_picture_number;
		}
	}
	else
	{
		av_free_packet(&clip->packet);
		if(clip->loop)
		{
			av_seek_frame(clip->formatCtx, clip->streamIndex, 0, AVSEEK_FLAG_FRAME);
			printf("Looped clip: %s\n", clip->filename);
		}
	}

	return 0;
}

// Print the video clip animation
void printClipInfo(VideoClip *clip)
{
	printf("FILENAME: %s\n", clip->filename);
	printf("Duration: ");
	if (clip->formatCtx->duration != AV_NOPTS_VALUE)
	{
		int64 duration = clip->formatCtx->duration + 
		(clip->formatCtx->duration <= INT64_MAX - 5000 ? 5000 : 0);
		int secs = duration / AV_TIME_BASE;
		int us = duration % AV_TIME_BASE;
		int mins = secs / 60;
		secs %= 60;
		int hours = mins / 60;
		mins %= 60;
		printf("%02d:%02d:%02d.%02d\n", hours, mins, secs, (100 * us) / AV_TIME_BASE);
	}
	if (clip->formatCtx->start_time != AV_NOPTS_VALUE)
	{
		int secs, us;
		printf("Start time: ");
		secs = clip->formatCtx->start_time / AV_TIME_BASE;
		us   = llabs(clip->formatCtx->start_time % AV_TIME_BASE);
		printf("%d.%02d\n", secs, (int) av_rescale(us, 1000000, AV_TIME_BASE));
	}
	printf("Video bitrate: ");
	if (clip->formatCtx->bit_rate) printf("%d kb/s\n", (int64_t)clip->formatCtx->bit_rate / 1000);
	else printf("N/A\n");
	printf("Width/Height: %dx%d\n", clip->width, clip->height);
	printf("Real based framerate: %.2ffps\n", clip->framerate);
	printf("Average framerate: %.2fps\n", clip->avgFramerate);
	printf("Aspect Ratio: (%f), [%d:%d]\n", clip->aspectRatioF, 
	       clip->aspectRatioW, clip->aspectRatioH);
	printf("Number of frames: ");
	if(clip->frameCount) printf("%d\n", clip->frameCount);
	else printf("unknown\n");
	printf("\n");
}

void initVideoClip(VideoClip *clip, const char *file, bool loop)
{
	clip->formatCtx = NULL;
	if(avformat_open_input(&clip->formatCtx, file, NULL, NULL) != 0)
	{
		printf("Could not open file: %s.\n", file);
		exit(-1);
	}

	clip->filename = av_strdup(clip->formatCtx->filename);

	avformat_find_stream_info(clip->formatCtx, NULL);

	av_dump_format(clip->formatCtx, 0, file, 0); // DEBUG

	clip->streamIndex = -1;

	for(int i = 0; i < clip->formatCtx->nb_streams; ++i)
	{
		if(clip->formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			clip->streamIndex = i;
			clip->stream = clip->formatCtx->streams[clip->streamIndex];
			break;
		}
	}

	assert(clip->streamIndex != -1);
	AVCodecContext *codecCtxOrig = 
	clip->formatCtx->streams[clip->streamIndex]->codec;
	clip->codec = avcodec_find_decoder(codecCtxOrig->codec_id);
	
	clip->codecCtx	= avcodec_alloc_context3(clip->codec);
	avcodec_copy_context(clip->codecCtx, codecCtxOrig);

	clip->width = clip->codecCtx->width;
	clip->height = clip->codecCtx->height;

	avcodec_close(codecCtxOrig);

	avcodec_open2(clip->codecCtx, clip->codec, NULL);

	clip->frame = NULL;
	clip->frame = av_frame_alloc();

	clip->swsCtx = sws_getContext(clip->codecCtx->width,
	                              clip->codecCtx->height,
	                              clip->codecCtx->pix_fmt,
	                              clip->codecCtx->width,
	                              clip->codecCtx->height,
	                              AV_PIX_FMT_YUV420P,
	                              SWS_BILINEAR,
	                              NULL,
	                              NULL,
	                              NULL);

	clip->yPlaneSz = clip->codecCtx->width * clip->codecCtx->height;
	clip->uvPlaneSz = clip->codecCtx->width * clip->codecCtx->height / 4;
	clip->yPlane = (uint8 *)malloc(clip->yPlaneSz);
	clip->uPlane = (uint8 *)malloc(clip->uvPlaneSz);
	clip->vPlane = (uint8 *)malloc(clip->uvPlaneSz);

	clip->uvPitch = clip->codecCtx->width / 2;

	clip->srcRect;
	clip->srcRect.x = 0;
	clip->srcRect.y = 0;
	clip->srcRect.w = clip->codecCtx->width;
	clip->srcRect.h = clip->codecCtx->height;

	int windowWidth, windowHeight;
	SDL_GetWindowSize(window, &windowWidth, &windowHeight);
	SDL_Rect videoDestRect;
	clip->destRect.x = 0;
	clip->destRect.y = 0;
	clip->destRect.w = clip->srcRect.w;
	clip->destRect.h = clip->srcRect.h;

	if(!clip->texture) 
	{
		clip->texture = SDL_CreateTexture(renderer,
		                                  SDL_PIXELFORMAT_YV12,
		                                  SDL_TEXTUREACCESS_STREAMING,
		                                  clip->codecCtx->width,
		                                  clip->codecCtx->height);
	}

	// Use the video stream to get the real base framerate and average framerates
	clip->stream = clip->formatCtx->streams[clip->streamIndex];
	AVRational fr = clip->stream->r_frame_rate;
	clip->framerate = (float)fr.num / (float)fr.den;
	AVRational afr = clip->stream->avg_frame_rate;
	clip->avgFramerate = (float)afr.num / (float)afr.den;
	
	// Use av_reduce() to reduce a fraction to get the width and height of the clip's aspect ratio
	av_reduce(&clip->aspectRatioW, &clip->aspectRatioH,
	          clip->codecCtx->width,
	          clip->codecCtx->height,
	          128);
	clip->aspectRatioF = (float)clip->aspectRatioW / (float)clip->aspectRatioH;

	clip->loop = loop;

	clip->currentFrame = -1;

	clip->frameCount = clip->stream->nb_frames;

	avcodec_close(codecCtxOrig);
	// Decode the first video frame
	int frameFinished;
	bool gotFirstVideoFrame = false;
	while(!gotFirstVideoFrame)
	{
		if(av_read_frame(clip->formatCtx, &clip->packet) >= 0)
		{
			if(clip->packet.stream_index == clip->streamIndex)
			{
				do
				{
					avcodec_decode_video2(clip->codecCtx, clip->frame, &frameFinished, &clip->packet);
				} while(!frameFinished);

				AVPicture pict;
				pict.data[0] = clip->yPlane;
				pict.data[1] = clip->uPlane;
				pict.data[2] = clip->vPlane;
				pict.linesize[0] = clip->codecCtx->width;
				pict.linesize[1] = clip->uvPitch;
				pict.linesize[2] = clip->uvPitch;

				sws_scale(clip->swsCtx, (uint8 const * const *)clip->frame->data, 
				          clip->frame->linesize, 
				          0, clip->codecCtx->height, pict.data, pict.linesize);

				SDL_UpdateYUVTexture(clip->texture, NULL, clip->yPlane, 
				                     clip->codecCtx->width, clip->uPlane,
				                     clip->uvPitch, clip->vPlane, clip->uvPitch);

				clip->currentFrame = clip->frame->coded_picture_number;

				gotFirstVideoFrame = true;
			}
		}
	}
	av_free_packet(&clip->packet);
}

void setClipPosition(SDL_Window *window, VideoClip *clip, int x, int y)
{
	int width, height;
	SDL_GetWindowSize(window, &width, &height);
	if(x < 0 || y < 0 || x > width || y > height) return;
	clip->destRect.x = x;
	clip->destRect.y = y;
}

void copyRect(SDL_Rect *dest, SDL_Rect *src)
{
	dest->x = src->x;
	dest->y = src->y;
	dest->w = src->w;
	dest->h = src->h;
}

void setRenderColor(SDL_Renderer *renderer, tColor color, uint8 alpha)
{
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, alpha);
}

void setRenderColor(SDL_Renderer *renderer, tColor color)
{
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

// TODO: Clean drawClipTransformControls() up
void drawClipTransformControls(SDL_Renderer *renderer, VideoClip *clip)
{

	SDL_Rect r = clip->destRect;

	SDL_Rect i;
	SDL_Rect b;

	i.x = r.x - 2;
	i.y = r.y - 2;
	i.w = 4;
	i.h = 4;

	b.x = i.x - 1;
	b.y = i.y - 1;
	b.w = 7;
	b.h = 7;

	// TOP ROW
	{
		setRenderColor(renderer, tcBlack);
		SDL_RenderFillRect(renderer, &b);
		setRenderColor(renderer, tcWhite);
		SDL_RenderFillRect(renderer, &i);

		i.y = r.y + (r.h / 2);
		b.y = i.y - 1;

		setRenderColor(renderer, tcBlack);
		SDL_RenderFillRect(renderer, &b);
		setRenderColor(renderer, tcWhite);
		SDL_RenderFillRect(renderer, &i);

		i.y = r.y + r.h - i.h;
		b.y = i.y - 1;

		setRenderColor(renderer, tcBlack);
		SDL_RenderFillRect(renderer, &b);
		setRenderColor(renderer, tcWhite);
		SDL_RenderFillRect(renderer, &i);	
	}
	i.x = r.x + (r.w / 2) - i.w;
	b.x = i.x - 1;

	// MIDDLE ROW
	{
		i.y = r.y;
		b.y = i.y - 1;

		setRenderColor(renderer, tcBlack);
		SDL_RenderFillRect(renderer, &b);
		setRenderColor(renderer, tcWhite);
		SDL_RenderFillRect(renderer, &i);

		i.y = r.y + (r.h / 2) - i.h;
		b.y = i.y - 1;

		setRenderColor(renderer, tcBlack);
		SDL_RenderFillRect(renderer, &b);
		setRenderColor(renderer, tcWhite);
		SDL_RenderFillRect(renderer, &i);

		i.y = r.y + r.h - i.h;
		b.y = i.y - 1;

		setRenderColor(renderer, tcBlack);
		SDL_RenderFillRect(renderer, &b);
		setRenderColor(renderer, tcWhite);
		SDL_RenderFillRect(renderer, &i);
	}

	i.x = r.x + r.w - i.w;
	b.x = i.x - 1;

	// BOTTOM ROW
	{
		i.y = r.y - 2;
		b.y = i.y - 1;
		setRenderColor(renderer, tcBlack);
		SDL_RenderFillRect(renderer, &b);
		setRenderColor(renderer, tcWhite);
		SDL_RenderFillRect(renderer, &i);

		i.y = r.y + (r.h / 2);
		b.y = i.y - 1;

		setRenderColor(renderer, tcBlack);
		SDL_RenderFillRect(renderer, &b);
		setRenderColor(renderer, tcWhite);
		SDL_RenderFillRect(renderer, &i);

		i.y = r.y + clip->destRect.h - i.h;
		b.y = i.y - 1;

		setRenderColor(renderer, tcBlack);
		SDL_RenderFillRect(renderer, &b);
		setRenderColor(renderer, tcWhite);
		SDL_RenderFillRect(renderer, &i);
	}
}

void drawBorder(SDL_Renderer *renderer, Border b)
{
	SDL_RenderFillRect(renderer, &b.top);
	SDL_RenderFillRect(renderer, &b.bot);
	SDL_RenderFillRect(renderer, &b.left);
	SDL_RenderFillRect(renderer, &b.right);
}



// This creates a border who's top and bottom are designated on the specified border side
void buildBorder(Border *b, SDL_Rect *r, BorderSide bs)
{
	int height = 20;
	int width = 4;

	if(bs == BORDER_SIDE_INSIDE)
	{
		b->top.x = r->x;
		b->top.y = r->y;
		b->top.w = r->w;
		b->top.h = height;

		b->bot.x = r->x;
		b->bot.y = r->y + r->h - height;
		b->bot.w = r->w;
		b->bot.h = height; 

		b->left.x = r->x - width;
		b->left.y = r->y;
		b->left.w = width;
		b->left.h = r->h;

		b->right.x = r->x + r->w;
		b->right.y = r->y;
		b->right.w = width;
		b->right.h = r->h;
	}
	else if(bs == BORDER_SIDE_OUTSIDE)
	{
		b->top.x = r->x - width;
		b->top.y = r->y - height;
		b->top.w = r->w + (width * 2);
		b->top.h = height;

		b->bot.x = r->x - width;
		b->bot.y = r->y + r->h;
		b->bot.w = r->w + (width * 2);
		b->bot.h = height;

		b->left.x = r->x - width;
		b->left.y = r->y;
		b->left.w = width;
		b->left.h = r->h;

		b->right.x = r->x + r->w;
		b->right.y = r->y;
		b->right.w = width;
		b->right.h = r->h;
	}
}

// TODO: Still a bit janky, including borders
void dualLayout()
{
	const int bufferSpace = 5;

	int windowWidth, windowHeight, hwidth, hheight, backViewW, backViewH, 
	currentViewW, currentViewH, compositeViewW, compositeViewH;

	SDL_GetWindowSize(window, &windowWidth, &windowHeight);
	hwidth = windowWidth / 2;
	hheight = windowHeight / 2;

	// Find the nearest 16x9 (STC) resolution to fit the current and composite views into
	// STC: Need to make the aspect ratio the ar of the output composition.
	for(int w = 0, h = 0; 
	    w < (hwidth - (bufferSpace * 8)) && h < (hheight - (bufferSpace * 8));
	    w += 16, h += 9)
	{
		backViewW = w;
		backViewH = h;
	}

	// Create the view rectangles for the background of the current and composite views
	// Fit the current view background into an output aspect ratio rectangle on the left side
	currentViewBack.w = backViewW;
	currentViewBack.h = backViewH;	
	currentViewBack.x = bufferSpace + 
	(((hwidth - (bufferSpace * 2)) - currentViewBack.w) / 2);
	currentViewBack.y = bufferSpace + 
	(((hheight - (bufferSpace * 2)) - currentViewBack.h) / 2);

	// Fit the current view background into an output aspect ratio rectangle on the right side
	compositeViewBack.w = backViewW;
	compositeViewBack.h = backViewH;
	compositeViewBack.x = (windowWidth / 2) + bufferSpace + 
	(((hwidth - (bufferSpace * 2)) - compositeViewBack.w) / 2);
	compositeViewBack.y = bufferSpace + (((hheight - (bufferSpace * 2)) - 
	                                      compositeViewBack.h) / 2);

  // (??? STC ???) <
	// If the clip is bigger than the back views then we just resize it to be the back view
	// and "fit it to frame". Otherwise we keep the Global_VideoClip at it's original size.
	// NOTE: We may want to test this, perhaps clip that are bigger than the size of the wanted
	// composite should be clipped outside of the composite... 
	if(Global_VideoClip.width > backViewW || Global_VideoClip.height > backViewH)
	{
		// Find the nearest rectangle to fit the clip's aspect ratio into the view backgrounds
		for(int w = 0, h = 0; 
		    w <= currentViewBack.w && h <= currentViewBack.h; 
		    w += Global_VideoClip.aspectRatioW, h += Global_VideoClip.aspectRatioH)
		{
			currentViewW = w;
			currentViewH = h;
		}

		// Find the nearest rectangle to fit the clip's aspect ratio into the view backgrounds
		for(int w = 0, h = 0; 
		    w <= compositeViewBack.w && h <= compositeViewBack.h; 
		    w += Global_VideoClip.aspectRatioW, h += Global_VideoClip.aspectRatioH)
		{
			compositeViewW = w;
			compositeViewH = h;
		}
	}
	else
	{
		currentViewW = Global_VideoClip.codecCtx->width;
		currentViewH = Global_VideoClip.codecCtx->height;

		compositeViewW = Global_VideoClip.codecCtx->width;
		compositeViewH = Global_VideoClip.codecCtx->height;
	}
	// (??? STC ???)) >

	// Put the current clip view on the right hand side and center it in the current view background
	currentView.w = currentViewW;
	currentView.h = currentViewH;
	currentView.x = currentViewBack.x + ((currentViewBack.w - currentView.w) / 2);
	currentView.y = currentViewBack.y + ((currentViewBack.h - currentView.h) / 2);
	copyRect(&Global_VideoClip.destRect, &currentView);
	//buildBorder(&currentBorder, &currentViewBack, BORDER_SIDE_OUTSIDE);

	// Put the composite clip view on the left hand side and center it in the composite 
	// view background
	compositeView.w = compositeViewW;
	compositeView.h = compositeViewH;
	compositeView.x = compositeViewBack.x + ((compositeViewBack.w - compositeView.w) / 2);
	compositeView.y = compositeViewBack.y + ((compositeViewBack.h - compositeView.h) / 2);
	//buildBorder(&compositeBorder, &compositeViewBack, BORDER_SIDE_OUTSIDE);

	// Put the file browser view in the bottom right portion of the screen
	browserView.x = bufferSpace;
	browserView.y = (windowHeight / 2) + bufferSpace;
	browserView.w = (windowWidth / 6) - (bufferSpace * 2);
	browserView.h = (windowHeight / 2) - (bufferSpace * 2);
	//buildBorder(&browserBorder, &browserView, BORDER_SIDE_INSIDE);

	// Put the timeline clip view in the center bottom portion of the screen between the file 
	// browser view and the track effects view
	timelineView.x = (windowWidth / 6) + bufferSpace;
	timelineView.y = (windowHeight / 2) + bufferSpace;
	timelineView.w = ((windowWidth / 6) * 4) - (bufferSpace * 2);
	timelineView.h = (windowHeight / 2) - (bufferSpace * 2);
	//buildBorder(&timelineBorder, &timelineView, BORDER_SIDE_INSIDE);

	// Put the track effects view in the bottom portion of the screen to the left of the 
	// timeline view
	effectsView.x = ((windowWidth / 6) * 5) + bufferSpace;
	effectsView.y = (windowHeight / 2) + bufferSpace;
	effectsView.w = (windowWidth / 6) - (bufferSpace * 2);
	effectsView.h = (windowHeight / 2) - (bufferSpace * 2);
	//buildBorder(&effectsBorder, &effectsView, BORDER_SIDE_INSIDE);
}

void singleLayout()
{
	const int bufferSpace = 10;

	int windowWidth, windowHeight, hwidth, hheight, backViewW, backViewH, 
	compositeViewW, compositeViewH;

	SDL_GetWindowSize(window, &windowWidth, &windowHeight);
	hwidth = windowWidth / 2;
	hheight = windowHeight / 2;

	// Put the timeline clip view in the bottom half of the screen
	timelineView.x = bufferSpace;
	timelineView.y = (windowHeight / 2) + bufferSpace;
	timelineView.w = (windowWidth) - (bufferSpace * 2);
	timelineView.h = (windowHeight / 2) - (bufferSpace * 2);
	//buildBorder(&timelineBorder, &timelineView, BORDER_SIDE_INSIDE);

	// Find the nearest 16x9 (STC) resolution to fit the composite views into
	// STC: Need to make the aspect ratio the aspect ratio of the output composition.
	for(int w = 0, h = 0; 
	    w < (windowWidth / 2) && h < (hheight - (bufferSpace * 2));
	    w += 16, h += 9)
	{
		backViewW = w;
		backViewH = h;
	}

	// Fit the current view background into an output aspect ratio in the top middle
	compositeViewBack.w = backViewW;
	compositeViewBack.h = backViewH;
	compositeViewBack.x = (windowWidth / 2) - (compositeViewBack.w / 2);
	compositeViewBack.y = (windowHeight / 4) - (compositeViewBack.h / 2);

	// (??? STC ???) <
  // If the clip is bigger than the back views then we just resize it to be the back view
  // and "fit it to frame". Otherwise we keep the Global_VideoClip at it's original size.
  // NOTE: We may want to test this, perhaps clips that are bigger than the size of the wanted
  // composite should be clipped outside of the composite... 
	if(Global_VideoClip.width > backViewW || Global_VideoClip.height > backViewH)
	{
				// Find the nearest rectangle to fit the clip's aspect ratio into the view backgrounds
		for(int w = 0, h = 0; 
		    w <= compositeViewBack.w && h <= compositeViewBack.h; 
		    w += Global_VideoClip.aspectRatioW, h += Global_VideoClip.aspectRatioH)
		{
			compositeViewW = w;
			compositeViewH = h;
		}
	}
	else
	{
		compositeViewW = Global_VideoClip.codecCtx->width;
		compositeViewH = Global_VideoClip.codecCtx->height;
	}
	// (??? STC ???)) >

	// Put the composite clip view on the left hand side and center it in the composite 
	// view background
	compositeView.w = compositeViewW;
	compositeView.h = compositeViewH;
	compositeView.x = compositeViewBack.x + ((compositeViewBack.w - compositeView.w) / 2);
	compositeView.y = compositeViewBack.y + ((compositeViewBack.h - compositeView.h) / 2);
	copyRect(&Global_VideoClip.destRect, &compositeView);
  //buildBorder(&compositeBorder, &compositeViewBack, BORDER_SIDE_OUTSIDE);

	// Put the file browser view in the top right portion (1/6th) of the screen
	browserView.x = bufferSpace;
	browserView.y = bufferSpace;
	browserView.w = compositeViewBack.x - (bufferSpace * 2);
	browserView.h = (windowHeight / 2) - (bufferSpace * 2);
  //buildBorder(&browserBorder, &browserView, BORDER_SIDE_INSIDE);

	// Put the track effects view in the top left portion (1/6th) of the screen
	effectsView.x = compositeViewBack.x + compositeViewBack.w + bufferSpace;
	effectsView.y = bufferSpace;
	effectsView.w = windowWidth - effectsView.x - bufferSpace;
	effectsView.h = (windowHeight / 2) - (bufferSpace * 2);
	//buildBorder(&effectsBorder, &effectsView, BORDER_SIDE_INSIDE);
}

void resizeAllWindowElements()
{
	if(Global_Layout == LAYOUT_SINGLE) singleLayout();
	else if(Global_Layout == LAYOUT_DUAL) dualLayout();
}

// TODO: DO NOT resizeAllWinowElements() each time a clip is changed
// The reason we do this is because when the clip is reinitialized, the rectangles for the
// views are not updated to hold the original aspect ratio of the clip into a rectangle
// that can be fit inside the backviews of the composite and/or current views.
internal void HandleEvents(SDL_Event event, VideoClip *clip)
{
	if(SDL_PollEvent(&event))
	{
		if(event.type == SDL_QUIT) Global_Running = false;
		if(event.type == SDL_KEYDOWN)
		{
			SDL_Keycode key = event.key.keysym.sym;
			switch(key)
			{
				case SDLK_ESCAPE:
				{
					Global_Running = false;
				} break;
				case SDLK_SPACE:
				{
					if(!Global_Paused) Global_Paused = true;
					else Global_Paused = false;
				} break;
				case SDLK_t:
				{
					if(!Global_Show_Transform_Tool) Global_Show_Transform_Tool = true;
					else Global_Show_Transform_Tool = false;
				} break;
				case SDLK_1:
				{
					freeVideoClipFull(&Global_VideoClip);
					Global_VideoClip = {};
					initVideoClip(&Global_VideoClip, Anime404mp4, true);
					resizeAllWindowElements();

				} break;
				case SDLK_2:
				{
					freeVideoClipFull(&Global_VideoClip);
					Global_VideoClip = {};
					initVideoClip(&Global_VideoClip, h2bmp4, true);
					resizeAllWindowElements();

				} break;
				case SDLK_3:
				{
					freeVideoClipFull(&Global_VideoClip);
					Global_VideoClip = {};
					initVideoClip(&Global_VideoClip, kiloshelos, true);
					resizeAllWindowElements();

				} break;
				case SDLK_4:
				{
					freeVideoClipFull(&Global_VideoClip);
					Global_VideoClip = {};
					initVideoClip(&Global_VideoClip, dance, true);
					resizeAllWindowElements();

				} break;
				case SDLK_5:
				{
					freeVideoClipFull(&Global_VideoClip);
					Global_VideoClip = {};
					initVideoClip(&Global_VideoClip, trump, true);
					resizeAllWindowElements();

				} break;
				case SDLK_6:
				{
					freeVideoClipFull(&Global_VideoClip);
					Global_VideoClip = {};
					initVideoClip(&Global_VideoClip, nggyu, true);
					resizeAllWindowElements();

				} break;
				case SDLK_7:
				{
					freeVideoClipFull(&Global_VideoClip);
					Global_VideoClip = {};
					initVideoClip(&Global_VideoClip, froggy, true);
					resizeAllWindowElements();

				} break;
				case SDLK_8:
				{
					freeVideoClipFull(&Global_VideoClip);
					Global_VideoClip = {};
					initVideoClip(&Global_VideoClip, groggy, true);
					resizeAllWindowElements();

				} break;
				case SDLK_9:
				{
					freeVideoClipFull(&Global_VideoClip);
					Global_VideoClip = {};
					initVideoClip(&Global_VideoClip, ruskie, true);
					resizeAllWindowElements();

				} break;
				case SDLK_0:
				{
					freeVideoClipFull(&Global_VideoClip);
					Global_VideoClip = {};
					initVideoClip(&Global_VideoClip, watamote, true);
					resizeAllWindowElements();

				} break;
				case SDLK_EQUALS:
				{
					if(Global_Layout != LAYOUT_SINGLE)
					{
						Global_Layout = LAYOUT_SINGLE;
						resizeAllWindowElements();
					}
				} break;
				case SDLK_MINUS:
				{
					if(Global_Layout != LAYOUT_DUAL)
					{
						Global_Layout = LAYOUT_DUAL;
						resizeAllWindowElements();
					}
				} break;
			}
		}
		if(event.type == SDL_WINDOWEVENT)
		{
			switch(event.window.event)
			{
				case SDL_WINDOWEVENT_SIZE_CHANGED:
				case SDL_WINDOWEVENT_RESIZED:
				{
					resizeAllWindowElements();
				} break;
				case SDL_WINDOWEVENT_FOCUS_LOST:
				{
					Global_Paused = true;
				} break;
			}
		}
	}
}

int main(int argc, char **argv)
{
	printf("Hello world.\n\n");

	av_register_all();

	SDL_Init(SDL_INIT_VIDEO);
	TTF_Init();

	window = SDL_CreateWindow("Mouse", 
	                          SDL_WINDOWPOS_CENTERED, 
	                          SDL_WINDOWPOS_CENTERED, 
	                          1920, 1080, 
	                          SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_MAXIMIZED);
	int windowWidth, windowHeight;

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

	SDL_Event event = {};

	const char *fname;
	if(argv[1]) fname = argv[1];
	else fname = Anime404mp4; // DEBUG FILENAME

	// TODO: List of clips to pass around
	initVideoClip(&Global_VideoClip, Anime404mp4, true);
	printClipInfo(&Global_VideoClip);

	resizeAllWindowElements();

	Global_Paused = true; // DEBUG
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	// TODO: Build a font map
	TTF_Font *font = TTF_OpenFont("../res/fonts/Anaheim-Regular.ttf", 72);
	int fontW, fontH;
	const char *testText = "Hello world!";
	TTF_SizeText(font, testText, &fontW, &fontH);
	SDL_Color color = { 0, 0, 0};
	SDL_Surface *textSurface = TTF_RenderText_Blended(font, testText, color);
	SDL_Texture *textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
	SDL_Rect textRect;
	textRect.x = 10;
	textRect.y = 10;
	textRect.w = textSurface->w;
	textRect.h = textSurface->h;

	SDL_FreeSurface(textSurface);

	while(Global_Running)
	{
		HandleEvents(event, &Global_VideoClip);
		SDL_GetWindowSize(window, &windowWidth, &windowHeight);
		if(!Global_Paused)
		{
			playVideoClip(&Global_VideoClip);
		}

		//printf("current frame: %d\n", Global_VideoClip.currentFrame);

		SDL_SetRenderDrawColor(renderer, 
		                       tcBackground.r, 
		                       tcBackground.g, 
		                       tcBackground.b, 
		                       tcBackground.a);
		SDL_RenderClear(renderer);

		setRenderColor(renderer, tcBlack);
		if(Global_Layout == LAYOUT_DUAL) SDL_RenderFillRect(renderer, &currentViewBack);
		SDL_RenderFillRect(renderer, &compositeViewBack);

		setRenderColor(renderer, tcRed);
		if(Global_Layout == LAYOUT_DUAL) SDL_RenderFillRect(renderer, &currentView);
		setRenderColor(renderer, tcBlue);
		SDL_RenderFillRect(renderer, &compositeView);

		// TODO: Render copy list of videos....
		if(Global_Layout == LAYOUT_DUAL) SDL_RenderCopy(renderer, 
		                                                Global_VideoClip.texture, 
		                                                &Global_VideoClip.srcRect, 
		                                                &currentView);
		SDL_RenderCopy(renderer, Global_VideoClip.texture, &Global_VideoClip.srcRect, &compositeView);

		setRenderColor(renderer, tcView);
		SDL_RenderFillRect(renderer, &browserView);
		SDL_RenderFillRect(renderer, &timelineView);
		SDL_RenderFillRect(renderer, &effectsView);
		
		setRenderColor(renderer, tcBorder);
		drawBorder(renderer, currentBorder);
		drawBorder(renderer, compositeBorder);
		drawBorder(renderer, browserBorder);
		drawBorder(renderer, timelineBorder);
		drawBorder(renderer, effectsBorder);

		if(Global_Show_Transform_Tool)
		{
			drawClipTransformControls(renderer, &Global_VideoClip);
		}

		SDL_RenderCopy(renderer, textTexture, NULL, &textRect);

		SDL_RenderPresent(renderer);
	}

	SDL_Quit();
	TTF_Quit();

	freeVideoClipFull(&Global_VideoClip);
	
	printf("\nGoodbye.\n");

	return 0;
}

