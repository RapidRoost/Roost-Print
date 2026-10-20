#include "I18N.hpp"
#include "GUI_App.hpp"

namespace Slic3r { namespace GUI { 

namespace I18N {

bool is_chinese()
{
	static std::string cached_lang;
	static bool cached_result = false;
	
	// Get current language setting
	std::string lang = wxGetApp().app_config ? wxGetApp().app_config->get("language") : "";
	
	// Cache the result to avoid repeated string comparisons
	if (lang != cached_lang) {
		cached_lang = lang;
		// Check for Chinese language codes (zh_CN, zh_TW, etc.)
		cached_result = (lang.find("zh_CN") == 0);
	}
	
	return cached_result;
}

wxString debrand(const wxString &in)
{
	if (!in.Contains("Creality"))
		return in;
	wxString s = in;
	// The app's own name.
	s.Replace("Creality3DSlicer", "Roost Print");
	s.Replace("CrealityPrint", "Roost Print");
	s.Replace("Creality Print", "Roost Print");
	// Creality's cloud.
	s.Replace("CrealityCloud", "Cloud");
	s.Replace("Creality Cloud", "Cloud");
	// Filament advice: the shop runs on Polymaker.
	s.Replace("Creality filament", "Polymaker filament");
	// "Creality printer", "Creality K2 Plus": just the printer.
	s.Replace("Creality printers", "printers");
	s.Replace("Creality printer", "printer");
	s.Replace("Creality ", "");
	// Anything left ("Creality" on its own).
	s.Replace("Creality", "Rapid Roost");
	return s;
}

} // namespace I18N

wxString L_str(const std::string &str)
{
	//! Explicitly specify that the source string is already in UTF-8 encoding
	return I18N::roost_tr(wxString(str.c_str(), wxConvUTF8));
}

} }
