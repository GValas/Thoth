#pragma once
#include <vector>

//! Calibrated LSV leverage surface L(S,t): one log-spot grid layer per
//! calibration date, in the same layout as the Dupire local-vol node (grid point
//! i of layer k sits at log-spot = (offset_k + i) * ln_step_k). Built by
//! Single::CalibrateLeverage; consumed by the MCL leverage node (layer-indexed,
//! the calibration dates ARE the diffusion dates) and by the PDE ADI
//! coefficients (arbitrary (S,t) lookup, linear in time between layers).
class LeverageSurface
{
  private:
    std::vector<double> _t_list;                  //!< layer time in years (ascending, first = 0)
    std::vector<double> _ln_step_list;            //!< per-layer log-spot grid spacing
    std::vector<long> _offset_list;               //!< per-layer signed index of the first grid point
    std::vector<std::vector<double>> _level_list; //!< per-layer leverage values on the grid

  public:
    //! append one calibrated layer (build order = date order)
    void PushLayer( double T,
                    double LnStep,
                    long Offset,
                    std::vector<double> Levels );

    [[nodiscard]] size_t Layers() const { return _t_list.size(); }
    [[nodiscard]] double LayerTime( size_t K ) const { return _t_list[K]; }
    [[nodiscard]] double LnStep( size_t K ) const { return _ln_step_list[K]; }
    [[nodiscard]] long Offset( size_t K ) const { return _offset_list[K]; }
    [[nodiscard]] const std::vector<double>& Levels( size_t K ) const { return _level_list[K]; }

    //! leverage of layer K at spot S: clamped linear interpolation on the layer's
    //! log-spot grid (an extreme spot reuses the boundary leverage)
    [[nodiscard]] double LayerAt( size_t K, double S ) const;

    //! leverage at an arbitrary (S, T): each bracketing layer interpolated at S,
    //! then linear in time (flat beyond the first/last layer). PDE lookup.
    [[nodiscard]] double GetLeverage( double S, double T ) const;
};
