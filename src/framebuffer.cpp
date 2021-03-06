#include "framebuffer.h"

#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>


const Framebuffer::PixelFormatInfo Framebuffer::pixelFormatInfo[] = {
	{BAD_PIXELFORMAT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "bad pixel format"},
	{R8G8B8X8, 32, 4, 8, 8, 8, 8, 24, 16,  8,  0, "R8G8B8X8"},
	{X8R8G8B8, 32, 4, 8, 8, 8, 8, 16,  8,  0, 24, "X8R8G8B8"},
	{  R8G8B8, 24, 3, 8, 8, 8, 0, 24, 16,  8,  0, "R8G8B8"},
	{X1R5G5B5, 16, 2, 5, 5, 5, 1, 10,  5,  0, 15, "X1R5G5B5"},
	{  R5G6B5, 16, 2, 5, 6, 5, 0, 11,  5,  0,  0, "R5G6B5"},
	{   GREY8,  8, 1, 8, 0, 0, 0,  0,  0,  0,  0, "GREY8"},
};

Framebuffer::Framebuffer(const std::string & device)
	: m_frameBufferDevice(0)
	, m_frameBuffer(nullptr)
	, m_frameBufferSize(0)
	, m_format(BAD_PIXELFORMAT)
	, m_formatInfo(pixelFormatInfo[0])
{
	create(0, 0, 0, device);
}

Framebuffer::Framebuffer(uint32_t width, uint32_t height, uint32_t bitsPerPixel, const std::string & device)
	: m_frameBufferDevice(0)
	, m_frameBuffer(nullptr)
	, m_frameBufferSize(0)
	, m_format(BAD_PIXELFORMAT)
	, m_formatInfo(pixelFormatInfo[0])
{
	create(width, height, bitsPerPixel, device);
}

void Framebuffer::create(uint32_t width, uint32_t height, uint32_t bitsPerPixel, const std::string & device)
{
	std::cout << "Opening framebuffer " << device << "..." << std::endl;

	//open the framebuffer for reading/writing
	m_frameBufferDevice = open(device.c_str(), O_RDWR);
	if (m_frameBufferDevice <= 0) {
		std::cout << "Failed to open " << device << " for reading/writing!" << std::endl;
		m_frameBufferDevice = 0;
		return;
	}

	//get current mode information
	if (ioctl(m_frameBufferDevice, FBIOGET_VSCREENINFO, &m_currentMode)) {
		std::cout << "Failed to read variable mode information!" << std::endl;
	}
	else {
		std::cout << "Original mode is " << m_currentMode.xres << "x" << m_currentMode.yres << "@" << m_currentMode.bits_per_pixel;
		std::cout << " with virtual resolution " << m_currentMode.xres_virtual << "x" << m_currentMode.yres_virtual << "." << std::endl;
	}
	//store screen mode for restoring it
	memcpy(&m_oldMode, &m_currentMode, sizeof(fb_var_screeninfo));

	//change screen mode. check if the user passed some values
	if (width != 0) {
		m_currentMode.xres = width;
	}
	if (height != 0) {
		m_currentMode.yres = height;
	}
	if (bitsPerPixel != 0) {
		m_currentMode.bits_per_pixel = bitsPerPixel;
	}
	m_currentMode.xres_virtual = m_currentMode.xres;
	m_currentMode.yres_virtual = m_currentMode.yres;
	if (ioctl(m_frameBufferDevice, FBIOPUT_VSCREENINFO, &m_currentMode)) {
		std::cout << "Failed to set mode to " << m_currentMode.xres << "x" << m_currentMode.yres << "@" << m_currentMode.bits_per_pixel << "!" << std::endl;
	}
	
	//get fixed screen information
	if (ioctl(m_frameBufferDevice, FBIOGET_FSCREENINFO, &m_fixedMode)) {
		std::cout << "Failed to read fixed mode information!" << std::endl;
	}
	
	//try to match an internal pixel format to the mode we got
	m_format = screenInfoToPixelFormat(m_currentMode);
	// fix rpi blue screen, unknown why
	m_format = X8R8G8B8;
	//check if we can use it
	if (m_format == BAD_PIXELFORMAT) {
		std::cout << "Unusable pixel format!" << std::endl;
		destroy();
		return;
	}
	m_formatInfo = pixelFormatInfo[m_format];

	//map framebuffer into user memory.
	m_frameBufferSize = m_currentMode.yres * m_fixedMode.line_length;
	m_frameBuffer = static_cast<uint8_t *>(mmap(nullptr, m_frameBufferSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_frameBufferDevice, 0));
	if (m_frameBuffer == MAP_FAILED) {
		std::cout << "Failed to map framebuffer to user memory!" << std::endl;
		destroy();
		return;
	}
	
	//dump some info
	std::cout << "Opened a " << m_currentMode.xres << "x" << m_currentMode.yres << "@" << m_currentMode.bits_per_pixel << " display." << std::endl;
	std::cout << "Pixel format is " << m_formatInfo.name << "." << std::endl;

	//draw blue debug rectangle
	/*for (int y = 0; y < m_currentMode.yres; ++y) {
		for (int x = 0; x < m_currentMode.xres; ++x) {
			((uint32_t *)m_frameBuffer)[y * m_currentMode.xres + x] = 0xFF1199DD;
		}
	}*/
}

Framebuffer::PixelFormat Framebuffer::screenInfoToPixelFormat(const struct fb_var_screeninfo & screenInfo)
{
	if (screenInfo.bits_per_pixel == 32) {
		if (screenInfo.transp.offset >= 24) {
			return R8G8B8X8;
		}
		else if (screenInfo.transp.offset <= 8) {
			return X8R8G8B8;
		}
	}
	else if (screenInfo.bits_per_pixel == 24) {
		return R8G8B8;
	}
	else if (screenInfo.bits_per_pixel == 16) {
		if (screenInfo.transp.length == 0) {
			if (screenInfo.red.length == 6 || screenInfo.green.length == 6 || screenInfo.blue.length == 6) {
				return R5G6B5;
			}
			else {
				return X1R5G5B5;
			}
		}
		else if (screenInfo.transp.length == 1) {
			return X1R5G5B5;
		}
	}
	else if (screenInfo.bits_per_pixel == 15) {
		return X1R5G5B5;
	}
	return BAD_PIXELFORMAT;
}

uint8_t * Framebuffer::convertToPixelFormat(PixelFormat destFormat, const uint8_t * source, PixelFormat sourceFormat, size_t count)
{
	//create destination buffer
	const size_t destSize = count * pixelFormatInfo[destFormat].bytesPerPixel;
	uint8_t * dest = new uint8_t[destSize];
	//if source is destination format, just copy
	if (sourceFormat == destFormat) {
		memcpy(dest, source, destSize);
		return dest;
	}
	//create temporary 32bit R8G8B8X8 conversion buffer
	const size_t tempSize = count * pixelFormatInfo[R8G8B8X8].bytesPerPixel;
	uint32_t * temp = reinterpret_cast<uint32_t *>(new uint8_t[tempSize]);
	//check source format and convert to 32bit temporary buffer
	if (sourceFormat == GREY8) {
		uint32_t * tempPixel = temp;
		const uint8_t * srcPixel = source;
		for (size_t pixel = 0; pixel < count; ++pixel, tempPixel++, srcPixel++) {
			*tempPixel = *srcPixel << 24 | *srcPixel << 16 | *srcPixel << 8 | 0xff;
		}
	}
	else if (sourceFormat == X1R5G5B5) {
		uint32_t * tempPixel = temp;
		const uint16_t * srcPixel = (const uint16_t *)source;
		for (size_t pixel = 0; pixel < count; ++pixel, tempPixel++, srcPixel++) {
			*tempPixel = (*srcPixel & 0x7c00) << 14 | (*srcPixel & 0x3e0) << 11 | (*srcPixel & 0x1f) << 8 | 0xff;
		}
	}
	else if (sourceFormat == R5G6B5) {
		uint32_t * tempPixel = temp;
		const uint16_t * srcPixel = (const uint16_t *)source;
		for (size_t pixel = 0; pixel < count; ++pixel, tempPixel++, srcPixel++) {
			*tempPixel = (*srcPixel & 0xf800) << 14 | (*srcPixel & 0x7e0) << 11 | (*srcPixel & 0x1f) << 8 | 0xff;
		}
	}
	else if (sourceFormat == R8G8B8) {
		uint32_t * tempPixel = temp;
		const uint8_t * srcPixel = source;
		for (size_t pixel = 0; pixel < count; ++pixel, tempPixel++, srcPixel++) {
			*tempPixel = srcPixel[2] << 24 | srcPixel[1] << 16 | srcPixel[0] << 8 | 0xff;
		}
	}
	else if (sourceFormat == X8R8G8B8) {
		uint32_t * tempPixel = temp;
		const uint32_t * srcPixel = (const uint32_t *)source;
		for (size_t pixel = 0; pixel < count; ++pixel, tempPixel++, srcPixel++) {
			*tempPixel = *srcPixel << 8 | 0xff;
		}
	}
	else if (sourceFormat == R8G8B8X8) {
		memcpy(temp, source, tempSize);
	}
	//now convert to destination format
	if (destFormat == GREY8) {
		const uint8_t * tempPixel = (const uint8_t *)temp;
		uint8_t * destPixel = dest;
		for (size_t pixel = 0; pixel < count; ++pixel, destPixel++, tempPixel+=4) {
			*destPixel = ((tempPixel[2] + tempPixel[1] + tempPixel[0]) / 3) & 0xff;
		}
	}
	else if (destFormat == X1R5G5B5) {
		const uint8_t * tempPixel = (const uint8_t *)temp;
		uint16_t * destPixel = (uint16_t *)dest;
		for (size_t pixel = 0; pixel < count; ++pixel, destPixel++, tempPixel+=4) {
			*destPixel = 0x8000 | (tempPixel[3] & 0xf8) << 7 | (tempPixel[2] & 0xf8) << 2 | (tempPixel[1] & 0xf8) >> 3;
		}
	}
	else if (destFormat == R5G6B5) {
		const uint8_t * tempPixel = (const uint8_t *)temp;
		uint16_t * destPixel = (uint16_t *)dest;
		for (size_t pixel = 0; pixel < count; ++pixel, destPixel++, tempPixel+=4) {
			*destPixel = (tempPixel[3] & 0xf8) << 8 | (tempPixel[2] & 0xfc) << 3 | (tempPixel[1] & 0xf8) >> 3;
		}
	}
	else if (destFormat == R8G8B8) {
		const uint8_t * tempPixel = (const uint8_t *)temp;
		uint8_t * destPixel = dest;
		for (size_t pixel = 0; pixel < count; ++pixel, destPixel+=3, tempPixel+=4) {
			destPixel[0] = tempPixel[1];
			destPixel[1] = tempPixel[2];
			destPixel[2] = tempPixel[3];
		}
	}
	else if (destFormat == X8R8G8B8) {
		const uint32_t * tempPixel = (const uint32_t *)temp;
		uint32_t * destPixel = (uint32_t *)dest;
		for (size_t pixel = 0; pixel < count; ++pixel, destPixel++, tempPixel++) {
			*destPixel = *tempPixel << 8 | 0xff;
		}
	}
	else if (destFormat == R8G8B8X8) {
		memcpy(dest, temp, tempSize);
	}
	//free temporary buffer
	delete [] temp;
	//return destination buffer
	return dest;
}

uint8_t * Framebuffer::convertToFramebufferFormat(const uint8_t * source, PixelFormat sourceFormat, size_t count)
{
	return convertToPixelFormat(m_format, source, sourceFormat, count);
}

bool Framebuffer::isAvailable() const
{
	return (m_frameBuffer != nullptr && m_frameBufferDevice != 0);
}

uint32_t Framebuffer::getWidth() const
{
	return m_currentMode.xres;
}

uint32_t Framebuffer::getHeight() const
{
	return m_currentMode.yres;
}

Framebuffer::PixelFormat Framebuffer::getFormat() const
{
	return m_format;
}

Framebuffer::PixelFormatInfo Framebuffer::getFormatInfo() const
{
	return m_formatInfo;
}

void Framebuffer::clear(const uint8_t * color)
{
	//fill screen with color
	if (m_formatInfo.bytesPerPixel == 4) {
		uint32_t * dest = (uint32_t *)m_frameBuffer;
		const uint32_t destColor = *((uint32_t *)color);
		for (uint32_t line = 0; line < m_currentMode.yres; ++line) {
			uint32_t * destLine = dest;
			for (uint32_t pixel = 0; pixel < m_currentMode.xres; ++pixel, destLine++) {
				*destLine = destColor;
			}
			dest += m_fixedMode.line_length / 4;
		}
	}
	else if (m_formatInfo.bytesPerPixel == 3) {
		uint8_t * dest = m_frameBuffer;
		for (uint32_t line = 0; line < m_currentMode.yres; ++line) {
			uint8_t * destLine = dest;
			for (uint32_t pixel = 0; pixel < m_currentMode.xres; ++pixel, destLine+=3) {
				destLine[0] = color[0];
				destLine[1] = color[1];
				destLine[2] = color[2];
			}
			dest += m_fixedMode.line_length;
		}
	}
	else if (m_formatInfo.bytesPerPixel == 2) {
		uint16_t * dest = (uint16_t *)m_frameBuffer;
		const uint16_t destColor = *((uint16_t *)color);
		for (uint32_t line = 0; line < m_currentMode.yres; ++line) {
			uint16_t * destLine = dest;
			for (uint32_t pixel = 0; pixel < m_currentMode.xres; ++pixel, destLine++) {
				*destLine = destColor;
			}
			dest += m_fixedMode.line_length / 2;
		}
	}
	else if (m_formatInfo.bytesPerPixel == 1) {
		uint8_t * dest = m_frameBuffer;
		const uint8_t destColor = *color;
		for (uint32_t line = 0; line < m_currentMode.yres; ++line) {
			uint8_t * destLine = dest;
			for (uint32_t pixel = 0; pixel < m_currentMode.xres; ++pixel, destLine++) {
				*destLine = destColor;
			}
			dest += m_fixedMode.line_length;
		}
	}
}

void Framebuffer::blit(uint32_t x, uint32_t y, const uint8_t * data, uint32_t width, uint32_t height, Framebuffer::PixelFormat sourceFormat)
{
	if (isAvailable()) {
		//std::cout << "Blitting " << width << "x" << height << "@" << bpp << " image to [" << x "," << y << "]." << std::endl;
		//sanity checks for start position and source dimensions
		if (x >= m_currentMode.xres || width == 0) {
			return;
		}
		else if (y >= m_currentMode.yres || height == 0) {
			return;
		}
		//clip source rectangle to framebuffer dimensions
		if (x + width > m_currentMode.xres) {
			width = m_currentMode.xres - x;
		}
		if (y + height > m_currentMode.yres) {
			height = m_currentMode.yres - y;
		}
		//check what framebuffer format we're blitting to
		if (m_format == sourceFormat) {
			blit_copy(x, y, data, width, height);
		}
		else if (m_format == R8G8B8X8) {
			blit_R8G8B8X8(x, y, data, width, height, sourceFormat);
		}
		else if (m_format == X8R8G8B8) {
			blit_X8R8G8B8(x, y, data, width, height, sourceFormat);
		}
		else if (m_format == R8G8B8) {
			blit_R8G8B8(x, y, data, width, height, sourceFormat);
		}
		else if (m_format == X1R5G5B5) {
			blit_X1R5G5B5(x, y, data, width, height, sourceFormat);
		}
		else if (m_format == R5G6B5) {
			blit_R5G6B5(x, y, data, width, height, sourceFormat);
		}
	}
}

void Framebuffer::blit_copy(uint32_t x, uint32_t y, const uint8_t * data, uint32_t width, uint32_t height)
{
	//blitting to the same format. simple memcopy    
	const uint32_t srcLineLength = width * m_formatInfo.bytesPerPixel;
	uint8_t * dest = m_frameBuffer + (y + m_currentMode.yoffset) * m_fixedMode.line_length + (x + m_currentMode.xoffset) * m_formatInfo.bytesPerPixel;
	const uint32_t destLineLength = m_fixedMode.line_length;
	for (uint32_t line = 0; line < height; ++line) {
		memcpy(dest, data, srcLineLength);
		dest += destLineLength;
		data += srcLineLength;
	}
}

void Framebuffer::blit_R8G8B8X8(uint32_t x, uint32_t y, const uint8_t * data, uint32_t width, uint32_t height, Framebuffer::PixelFormat sourceFormat)
{
	const uint32_t srcLineLength = width * pixelFormatInfo[sourceFormat].bytesPerPixel;
	uint32_t * dest = reinterpret_cast<uint32_t *>(m_frameBuffer + (y + m_currentMode.yoffset) * m_fixedMode.line_length + (x + m_currentMode.xoffset) * m_formatInfo.bytesPerPixel);
	const uint32_t destLineLength = m_fixedMode.line_length / 4;
	//check source format
	if (sourceFormat == GREY8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint32_t * destLine = dest;
			const uint8_t * srcLine = data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine++) {
				*destLine = *srcLine << 24 | *srcLine << 16 | *srcLine << 8 | 0xff;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == X1R5G5B5) {
		for (uint32_t line = 0; line < height; ++line) {
			uint32_t * destLine = dest;
			const uint16_t * srcLine = (const uint16_t *)data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine++) {
				*destLine = (*srcLine & 0x7c00) << 14 | (*srcLine & 0x3e0) << 11 | (*srcLine & 0x1f) << 8 | 0xff;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == R5G6B5) {
		for (uint32_t line = 0; line < height; ++line) {
			uint32_t * destLine = dest;
			const uint16_t * srcLine = (const uint16_t *)data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine++) {
				*destLine = (*srcLine & 0xf800) << 14 | (*srcLine & 0x7e0) << 11 | (*srcLine & 0x1f) << 8 | 0xff;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == R8G8B8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint32_t * destLine = dest;
			const uint8_t * srcLine = data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine += 3) {
				*destLine = srcLine[2] << 24 | srcLine[1] << 16 | srcLine[0] << 8 | 0xff;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == X8R8G8B8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint32_t * destLine = dest;
			const uint32_t * srcLine = (uint32_t *)data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine++) {
				*destLine = *srcLine << 8 | 0xff;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
}

void Framebuffer::blit_X8R8G8B8(uint32_t x, uint32_t y, const uint8_t * data, uint32_t width, uint32_t height, Framebuffer::PixelFormat sourceFormat)
{
	const uint32_t srcLineLength = width * pixelFormatInfo[sourceFormat].bytesPerPixel;
	uint32_t * dest = reinterpret_cast<uint32_t *>(m_frameBuffer + (y + m_currentMode.yoffset) * m_fixedMode.line_length + (x + m_currentMode.xoffset) * m_formatInfo.bytesPerPixel);
	const uint32_t destLineLength = m_fixedMode.line_length / 4;
	//check source format
	if (sourceFormat == GREY8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint32_t * destLine = dest;
			const uint8_t * srcLine = data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine++) {
				*destLine = 0xff000000 | *srcLine << 16 | *srcLine << 8 | *srcLine;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == X1R5G5B5) {
		for (uint32_t line = 0; line < height; ++line) {
			uint32_t * destLine = dest;
			const uint16_t * srcLine = (const uint16_t *)data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine += 2) {
				*destLine = 0xff000000 | (*srcLine & 0x7c00) << 6 | (*srcLine & 0x3e0) << 3 | (*srcLine & 0x1f);
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == R5G6B5) {
		for (uint32_t line = 0; line < height; ++line) {
			uint32_t * destLine = dest;
			const uint16_t * srcLine = (const uint16_t *)data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine += 2) {
				*destLine = 0xff000000 | (*srcLine & 0xf800) << 5 | (*srcLine & 0x7e0) << 3 | (*srcLine & 0x1f);
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == R8G8B8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint32_t * destLine = dest;
			const uint8_t * srcLine = data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine += 3) {
				*destLine = 0xff000000 | srcLine[2] << 16 | srcLine[1] << 8 | srcLine[0];
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == R8G8B8X8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint32_t * destLine = dest;
			const uint32_t * srcLine = (const uint32_t *)data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine++) {
				*destLine = 0xff000000 | *srcLine >> 8;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
}

void Framebuffer::blit_R8G8B8(uint32_t x, uint32_t y, const uint8_t * data, uint32_t width, uint32_t height, Framebuffer::PixelFormat sourceFormat)
{
	const uint32_t srcLineLength = width * pixelFormatInfo[sourceFormat].bytesPerPixel;
	uint8_t * dest = m_frameBuffer + (y + m_currentMode.yoffset) * m_fixedMode.line_length + (x + m_currentMode.xoffset) * m_formatInfo.bytesPerPixel;
	const uint32_t destLineLength = m_fixedMode.line_length;
	//check source format
	if (sourceFormat == GREY8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint8_t * destLine = dest;
			const uint8_t * srcLine = data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine+=3, srcLine++) {
				destLine[0] = *srcLine;
				destLine[1] = *srcLine;
				destLine[2] = *srcLine;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	if (sourceFormat == X1R5G5B5) {
		for (uint32_t line = 0; line < height; ++line) {
			uint8_t * destLine = dest;
			const uint16_t * srcLine = (const uint16_t *)data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine += 2) {
				destLine[0] = (*srcLine & 0x7c00) >> 10;
				destLine[1] = (*srcLine & 0x03e0) >> 5;
				destLine[2] = (*srcLine & 0x001f);
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	if (sourceFormat == R5G6B5) {
		for (uint32_t line = 0; line < height; ++line) {
			uint8_t * destLine = dest;
			const uint16_t * srcLine = (const uint16_t *)data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine += 2) {
				destLine[0] = (*srcLine & 0xf800) >> 11;
				destLine[1] = (*srcLine & 0x07e0) >> 5;
				destLine[2] = (*srcLine & 0x001f);
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == R8G8B8X8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint8_t * destLine = dest;
			const uint32_t * srcLine = (uint32_t *)data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine+=3, srcLine++) {
				destLine[0] = srcLine[3];
				destLine[1] = srcLine[2];
				destLine[2] = srcLine[1];
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == X8R8G8B8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint8_t * destLine = dest;
			const uint32_t * srcLine = (uint32_t *)data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine+=3, srcLine++) {
				destLine[0] = srcLine[2];
				destLine[1] = srcLine[1];
				destLine[2] = srcLine[0];
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
}

void Framebuffer::blit_X1R5G5B5(uint32_t x, uint32_t y, const uint8_t * data, uint32_t width, uint32_t height, Framebuffer::PixelFormat sourceFormat)
{
	const uint32_t srcLineLength = width * pixelFormatInfo[sourceFormat].bytesPerPixel;
	uint16_t * dest = reinterpret_cast<uint16_t *>(m_frameBuffer + (y + m_currentMode.yoffset) * m_fixedMode.line_length + (x + m_currentMode.xoffset) * m_formatInfo.bytesPerPixel);
	const uint32_t destLineLength = m_fixedMode.line_length / 2;
	//check source format
	if (sourceFormat == GREY8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint16_t * destLine = dest;
			const uint8_t * srcLine = data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine++) {
				*destLine = 0x8000 | (*srcLine & 0xf8) << 7 | (*srcLine & 0xf8) << 2 | (*srcLine & 0xf8) >> 3;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	if (sourceFormat == R5G6B5) {
		for (uint32_t line = 0; line < height; ++line) {
			uint16_t * destLine = dest;
			const uint16_t * srcLine = (const uint16_t *)data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine++) {
				*destLine = 0x8000 | (*srcLine & 0xffc0) >> 1 | (*srcLine & 0x001f);
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == R8G8B8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint16_t * destLine = dest;
			const uint8_t * srcLine = data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine+=3) {
				*destLine = 0x8000 | (srcLine[2] & 0xf8) << 7 | (srcLine[1] & 0xf8) << 2 | (srcLine[0] & 0xf8) >> 3;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == R8G8B8X8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint16_t * destLine = dest;
			const uint8_t * srcLine = data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine+=4) {
				*destLine = 0x8000 | (srcLine[3] & 0xf8) << 7 | (srcLine[2] & 0xf8) << 2 | (srcLine[1] & 0xf8) >> 3;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == X8R8G8B8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint16_t * destLine = dest;
			const uint8_t * srcLine = data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine+=4) {
				*destLine = 0x8000 | (srcLine[2] & 0xf8) << 7 | (srcLine[1] & 0xf8) << 2 | (srcLine[0] & 0xf8) >> 3;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
}

void Framebuffer::blit_R5G6B5(uint32_t x, uint32_t y, const uint8_t * data, uint32_t width, uint32_t height, Framebuffer::PixelFormat sourceFormat)
{
	const uint32_t srcLineLength = width * pixelFormatInfo[sourceFormat].bytesPerPixel;
	uint16_t * dest = reinterpret_cast<uint16_t *>(m_frameBuffer + (y + m_currentMode.yoffset) * m_fixedMode.line_length + (x + m_currentMode.xoffset) * m_formatInfo.bytesPerPixel);
	const uint32_t destLineLength = m_fixedMode.line_length / 2;
	//check source format
	if (sourceFormat == GREY8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint16_t * destLine = dest;
			const uint8_t * srcLine = data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine++) {
				*destLine = (*srcLine & 0xf8) << 8 | (*srcLine & 0xfc) << 3 | (*srcLine & 0xf8) >> 3;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	if (sourceFormat == X1R5G5B5) {
		for (uint32_t line = 0; line < height; ++line) {
			uint16_t * destLine = dest;
			const uint16_t * srcLine = (const uint16_t *)data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine++) {
				*destLine = (*srcLine & 0x7ff0) << 1 | (*srcLine & 0x001f);
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == R8G8B8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint16_t * destLine = dest;
			const uint8_t * srcLine = data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine+=3) {
				*destLine = (srcLine[2] & 0xf8) << 8 | (srcLine[1] & 0xfc) << 3 | (srcLine[0] & 0xf8) >> 3;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == R8G8B8X8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint16_t * destLine = dest;
			const uint8_t * srcLine = data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine+=4) {
				*destLine = (srcLine[3] & 0xf8) << 8 | (srcLine[2] & 0xfc) << 3 | (srcLine[1] & 0xf8) >> 3;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
	else if (sourceFormat == X8R8G8B8) {
		for (uint32_t line = 0; line < height; ++line) {
			uint16_t * destLine = dest;
			const uint8_t * srcLine = data;
			for (uint32_t pixel = 0; pixel < width; ++pixel, destLine++, srcLine+=4) {
				*destLine = (srcLine[2] & 0xf8) << 8 | (srcLine[1] & 0xfc) << 3 | (srcLine[0] & 0xf8) >> 3;
			}
			dest += destLineLength;
			data += srcLineLength;
		}
	}
}

void Framebuffer::destroy()
{
	std::cout << "Closing framebuffer..." << std::endl;
	
	if (m_frameBuffer != nullptr && m_frameBuffer != MAP_FAILED) {
		munmap(m_frameBuffer, m_frameBufferSize);
	}
	m_frameBuffer = nullptr;
	m_frameBufferSize = 0;

	if (m_frameBufferDevice != 0) {
		//reset old screen mode
		ioctl(m_frameBufferDevice, FBIOPUT_VSCREENINFO, &m_oldMode);
		m_frameBufferDevice = 0;
		//close device
		close(m_frameBufferDevice);
	}
}

Framebuffer::~Framebuffer()
{
	destroy();
}
