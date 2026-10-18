#ifndef _AUTO_CONVERT_3MF_MGR_H
#define _AUTO_CONVERT_3MF_MGR_H
#include <string>
#include <vector>
namespace Slic3r {

class DynamicPrintConfig;
class Model;

namespace GUI {

enum class ConversionMode {
    STL_TO_3MF,
    _3MF_TO_3MF
};

class AutoConvert3mfMgr
{
public:
    void             start_conversion();
    void             convert_to_printer();
    void             on_arrange_job_finished();
    void             start_sequential_arrange();
    void             arrange_next_plate();
    void             auto_select_printer_preset(const std::string& printer_name);
    void             auto_change_printer_preset(const std::string& printer_name);
    void             do_necessary_arrange(int plate_index);
    std::vector<int> get_need_arrange_object_ids(int plate_index);
    void             set_serious_warning_state(bool has_serious_warning);

    void set_conversion_mode(ConversionMode mode) { m_conversion_mode = mode; }
    void set_printer_preset(const std::string& printer_preset_name) { m_printer_preset = printer_preset_name; }
    void set_output_3mf(const std::string& output_3mf_name) { m_output_3mf_name = output_3mf_name; }
    void set_filament_preset(const std::string& filament_preset_name) { m_filament_preset_name = filament_preset_name; }
    void set_process_preset(const std::string& process_preset_name) { m_process_preset_name = process_preset_name; }
    void auto_change_filament_and_process_preset();

private:
    ConversionMode                m_conversion_mode = ConversionMode::_3MF_TO_3MF;
    std::vector<std::vector<int>> m_plate_object;
    int                           m_current_plate_index;
    // store "from_loaded_id" value of ModelObject, when the ModelObject is loaded from 3mf file;
    // because after arrangement, the ModelObject in model.objects may be re sorted
    std::vector<std::vector<int>> m_need_arrange_object;
    bool                          m_has_serious_warnings = false;
    std::string                   m_printer_preset;
    std::string                   m_output_3mf_name;
    // stl to 3mf need the following two (filament and process) preset
    std::string m_filament_preset_name;
    std::string m_process_preset_name;
    void        on_all_plate_arrangement_finished();
    void        check_object_need_arrange_state(int plate_index);
};
} // namespace GUI
} // namespace Slic3r
#endif // _AUTO_CONVERT_3MF_MGR_H