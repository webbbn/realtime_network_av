
#include <iostream>

#include <boost/format.hpp>

#include "sdl_osd.hh"

// Draw everything based on a screen size of 1080p and scale it to the actual screen size.
static const uint16_t g_canonical_screen_width = 1920;
static const uint8_t g_font_size = 35;
static const uint8_t g_line_size = static_cast<uint8_t>(g_font_size * 1.25);
static const uint8_t g_text_border_width = 2;
static const char *g_mode_strings[] = {
  "Stabilize", // manual airframe angle with manual throttle
  "Acro",      // manual body-frame angular rate with manual throttle
  "Alt Hold",  // manual airframe angle with automatic throttle
  "Auto",      // fully automatic waypoint control using mission commands
  "Guided",    // fully automatic fly to coordinate or fly at velocity/direction using GCS immediate commands
  "Loiter",    // automatic horizontal acceleration with automatic throttle
  "RTL",       // automatic return to launching point
  "Circle",    // automatic circular flight with automatic throttle
  "Land",      // automatic landing with horizontal position control
  "Drift",     // semi-automous position, yaw and throttle control
  "Sport",     // manual earth-frame angular rate control with manual throttle
  "Flip",      // automatically flip the vehicle on the roll axis
  "Autotune",  // automatically tune the vehicle's roll and pitch gains
  "Poshold",   // automatic position hold with manual override, with automatic throttle
  "Brake",     // full-brake using inertial/GPS system, no pilot input
  "Throw",     // throw to launch mode using inertial/GPS system, no pilot input
  "Avoid",     // automatic avoidance of obstacles in the macro scale - e.g. full-sized aircraft
  "Guided",    // guided mode but only accepts attitude and altitude
};

SDLOSD::SDLOSD(const std::string &font_file, const std::string &home_dir_icon,
	       const std::string &north_arrow, SDL_Renderer *renderer,
	       std::shared_ptr<Telemetry> telem,
	       uint32_t display_width, uint32_t display_height) :
  m_renderer(renderer), m_font(0), m_font_bg(0), m_units_font(0), m_units_font_bg(0),
  m_telem(telem) {

  // We want the same text border width no matter what the display resolution
  m_text_border = static_cast<uint8_t>(ceil(g_text_border_width * g_canonical_screen_width /
					    static_cast<float>(display_width)));

  // Load the font and use the same fort for the text border.
  m_font = TTF_OpenFont(font_file.c_str(), g_font_size);
  m_font_bg = TTF_OpenFont(font_file.c_str(), g_font_size);
  m_units_font = TTF_OpenFont(font_file.c_str(), g_font_size / 2);
  m_units_font_bg = TTF_OpenFont(font_file.c_str(), g_font_size / 2);
  if ((m_font == NULL) || (m_font_bg == NULL) ||
      (m_units_font == NULL)  || (m_units_font_bg == NULL)) {
    std::cerr << "Warning: TTF_OpenFont() Failed: " << TTF_GetError() << std::endl;
  } else {
    TTF_SetFontOutline(m_font_bg, m_text_border);
    TTF_SetFontOutline(m_units_font_bg, m_text_border);
  }

  // Load the home direction array.
  m_home_arrow = new Texture(m_renderer);
  if (!m_home_arrow->load_from_file(home_dir_icon)) {
    std::cerr << "Error loading the home direction arrow image" << std::endl
	      << "  " << home_dir_icon << std::endl;
    delete m_home_arrow;
    m_home_arrow = 0;
  }

  // Load the north arrow
  m_north_arrow = new Texture(m_renderer);
  if (!m_north_arrow->load_from_file(north_arrow)) {
    std::cerr << "Error loading the north arrow image" << std::endl
	      << "  " << north_arrow << std::endl;
    delete m_north_arrow;
    m_north_arrow = 0;
  }

  // Scale the renderer based on a 1080p screen size
  float scale = static_cast<float>(display_width) / g_canonical_screen_width;
  SDL_RenderSetScale(m_renderer, scale, scale);
  m_num_lines = static_cast<uint8_t>((display_height / scale) / g_line_size);
  m_num_cols = static_cast<uint8_t>((display_width / scale) / g_font_size);
  m_scaled_screen_height = static_cast<uint32_t>(display_height / scale);
  std::cerr << "width: " << display_width << "  height: " << display_height
	    << "  scale: " << scale << "  num_lines: " << static_cast<int>(m_num_lines) << std::endl;
}

bool SDLOSD::add_text(const std::string &text, TTF_Font *font, TTF_Font *bg_font,
		      const SDL_Color &color, const SDL_Color &bg_color, int x, int y,
		      SDL_Rect &text_rect) {

  // Write the background text to a surface
  SDL_Surface *bg_surface = TTF_RenderText_Blended(bg_font, text.c_str(), bg_color);
  if (!bg_surface) {
    return false;
  }

  // Write the foreground text to a surface
  SDL_Surface *text_surface = TTF_RenderText_Blended(font, text.c_str(), color);
  if (!text_surface) {
    return false;
  }

  // Draw both surfaces to the final texture
  SDL_Rect rect = { m_text_border, m_text_border, text_surface->w, text_surface->h };
  SDL_SetSurfaceBlendMode(text_surface, SDL_BLENDMODE_BLEND); 
  SDL_BlitSurface(text_surface, NULL, bg_surface, &rect); 
  SDL_FreeSurface(text_surface); 
  SDL_Texture *text_texture = SDL_CreateTextureFromSurface(m_renderer, bg_surface);
  SDL_FreeSurface(bg_surface);
  if (!text_texture) {
    return false;
  }

  // Add the texture and rectangle to the list
  m_text_textures.push_back(text_texture);
  SDL_QueryTexture(text_texture, NULL, NULL, &text_rect.w, &text_rect.h);
  text_rect.x = x;
  text_rect.y = y;
  m_text_rects.push_back(text_rect);

  return true;
}

bool SDLOSD::add_text(const std::string &text, const std::string &units,
		      int8_t col, int8_t row, bool from_center,
		      uint8_t r, uint8_t g, uint8_t b, uint8_t a,
		      uint8_t rb, uint8_t gb, uint8_t bb, uint8_t ab) {
  int x;
  int y;
  if (from_center) {
    x = ((col < 0) ? ((m_num_cols / 2) + col - 1) : (col + m_num_cols / 2)) *
      g_font_size + g_font_size;
    //y = ((row < 0) ? ((m_num_lines / 2) + row - 1) : (row + m_num_lines / 2)) *
    //g_line_size + g_font_size;
    y = ((row < 0) ? m_num_lines + row - 1 : row) * g_line_size + g_font_size;
  } else {
    x = ((col < 0) ? m_num_cols + col - 1 : col) * g_font_size + g_font_size;
    y = ((row < 0) ? m_num_lines + row - 1 : row) * g_line_size + g_font_size;
  }
  SDL_Color color;
  color.r = r;
  color.g = g;
  color.b = b;
  color.a = a;
  SDL_Color background;
  background.r = rb;
  background.g = gb;
  background.b = bb;
  background.a = ab;

  // Ensure we have a font allocated
  if (!m_font || !m_font_bg || !m_units_font || !m_units_font_bg) {
    return false;
  }

  // Draw the value and the units
  SDL_Rect rect;
  return add_text(text, m_font, m_font_bg, color, background, x, y, rect) &&
    add_text(units, m_units_font, m_units_font_bg, color, background,
	     rect.x + rect.w, y + g_font_size / 3, rect);
}

// Get a value from the telmetry class and add it to the display
void SDLOSD::add_telemetry(const std::string &key, const std::string &format,
			   const std::string &units, int32_t x, int32_t y, bool from_center,
			   uint8_t r, uint8_t g, uint8_t b, uint8_t a,
			   uint8_t rb, uint8_t gb, uint8_t bb, uint8_t ab) {
  float value = 0;
  if (!m_telem->get_value(key, value)) {
    value = 0;
  }
  add_text(str(boost::format(format) % value), units, x, y, from_center,
	   r, g, b, a, rb, gb, bb, ab);
}

void SDLOSD::update() {

  // Draw the battery status text
  float remain = 0;
  bool low_bat = true;
  if (m_telem->get_value("battery_remaining", remain)) {
    low_bat = (remain < 0.15);
  }
  if (low_bat) {
    add_telemetry("voltage_battery", "%4.1f", "V", 0, 0, false, 255, 0, 0);
    add_telemetry("battery_remaining", "%4.1f", "%", 0, 1, false, 255, 0, 0);
  } else {
    add_telemetry("voltage_battery", "%4.1f", "V", 0, 0, false, 0, 255, 0);
    add_telemetry("battery_remaining", "%4.1f", "%", 0, 1, false, 0, 255, 0);
  }
  add_telemetry("current_battery", "%4.1f", "A", 0, 2, false);

  // Draw the distance/speed text
  add_telemetry("distance", "%6.2f", "km", -5, 0, false);
  add_telemetry("speed", "%6.2f", "km/h", -5, 1, false);
  add_telemetry("altitude", "%6.2f", "m", -5, 2, false);
  add_telemetry("relative_altitude", "%6.2f", "m", -5, 3, false);

  // Draw the armed/disarmed icon.
  float armed_val = 0;
  if (m_telem->get_value("armed", armed_val) && (armed_val > 0.0)) {
    add_text("Armed", "", 0, -2, false, 255, 128, 128);
  } else {
    add_text("Disarmed", "", 0, -2, false, 0, 255, 0);
  }

  // Draw the mode text.
  float mode_val = 0;
  if (!m_telem->get_value("mode", mode_val)) {
    mode_val = 0;
  }
  uint8_t mode = static_cast<uint8_t>(mode_val);
  add_text(g_mode_strings[mode], "", 0, -1, false);

  // Draw the GPS location
  float latitude;
  float longitude;
  m_telem->get_value("latitude", latitude);
  m_telem->get_value("longitude", longitude);
  float deg = std::max(std::min(latitude, 90.0F), -90.0F);
  int32_t deg_int = static_cast<int32_t>(deg);
  float min = (deg - static_cast<float>(deg_int)) * 60.0;
  int32_t min_int = static_cast<int32_t>(min);
  float sec = (min - static_cast<float>(min_int)) * 60.0;
  add_text((deg < 0) ? "S" : "N", "", -10, -2, true);
  add_text(str(boost::format("%2d") % abs(deg_int)), "o", -9, -2, true);
  add_text(str(boost::format("%2d") % abs(min_int)), "'", -7, -2, true);
  add_text(str(boost::format("%5.2f") % abs(sec)), "\"", -5, -2, true);
  deg = std::max(std::min(longitude, 180.0F), -180.0F);
  deg_int = static_cast<int32_t>(deg);
  min = (deg - static_cast<float>(deg_int)) * 60.0;
  min_int = static_cast<int32_t>(min);
  sec = (min - static_cast<float>(min_int)) * 60.0;
  add_text((deg < 0) ? "W" : "E", "", 2, -2, true);
  add_text(str(boost::format("%2d") % abs(deg_int)), "o", 3, -2, true);
  add_text(str(boost::format("%2d") % abs(min_int)), "'", 6, -2, true);
  add_text(str(boost::format("%5.2f") % abs(sec)), "\"", 8, -2, true);
}

void SDLOSD::draw() {

  // Render the text
  for (size_t t = 0; t < m_text_textures.size(); ++t) {
    SDL_RenderCopy(m_renderer, m_text_textures[t], NULL, &m_text_rects[t]);
    SDL_DestroyTexture(m_text_textures[t]);
  }
  m_text_textures.clear();
  m_text_rects.clear();

  // Render the home arrow
  if (m_home_arrow) {
    float home_direction = 0;
    m_telem->get_value("home_direction", home_direction);
    m_home_arrow->render((g_canonical_screen_width - m_home_arrow->width()) / 2, 10, 0,
			 home_direction);
  }

  // Render the north arrow
  if (m_north_arrow) {
    float north_direction = 0;
    m_telem->get_value("heading", north_direction);
    m_north_arrow->render((g_canonical_screen_width - m_north_arrow->width()) / 2,
			  m_scaled_screen_height - m_north_arrow->height() - 40,
			  0, 360.0 - north_direction);
  }
}
