#pragma once
#include "pricer.hpp"

class VarianceSwap;

/*
|| V(X,t) : V_t = A(X).V_X + B(X).V_XX + C(X).V
||          with V(X_min,t) = BC_min(t), V(X_max,t) = BC_max(t), V(X,T) = FC(T)
|| --------------------------
|| X = Phi(x) => V(X) = u(x)
|| Phi(x) = X_0 + cc * sinh(aa * x + bb)
|| a(x) = A(Phi(x)) / Phi_x(x)^2
|| b(x) = B(Phi(x)) / Phi_x(x) -  A(Phi(x)) * Phi_xx(x) / Phi_x(x)^3
|| c(x) = C(Phi(x))
|| --------------------------
|| u(x,t) : u_t = a(x).u_x + b(x).u_xx + c(x).u,
||          with u(x_min,t) = bc_min(t), u(x_max,t) = bc_max(t), u(x,T) = FC(T)
|| --------------------------
|| u_t = L.u
||     = L_d(j).u(j-1) + L_m(j).u(j)  + L_u(j).u(j+1)
|| --------------------------
|| U_0.(I + k.theta.L) = U_1.(I - k.theta.L)
|| U_0.T_0             = U_1.T_1
*/

class PricerPDE : public Pricer
{

  private:
    // market data
    double _s;      // spot
    double _v;      // vol
    double _r;      // carry / drift rate (underlying ccy, quanto-corrected)
    double _r_disc; // discount rate (premium ccy); equals r for non-quanto
    date _maturity;
    bool _is_american;

    // outputs
    double _price;
    double GetGridPrice( double x, la_vector* Uy );

    // original grid
    double _x_0_orig; // we look for V(X_0) = u(x_0)
    double _x_max_orig;
    double _x_min_orig;
    double _t_max;
    double _v_up; // up border
    double _v_dw; // dw border

    // transformation settings
    double _c1;
    double _c2;
    double _aa; // alpha
    double _bb; // = c1 - c2
    double _cc; // = c2

    // transformation function derivatives & inverse : X = Phi(x), x = Psi(X)
    inline double Phi( double x );
    inline double Phi_x( double x );
    inline double Phi_xx( double x );
    inline double Psi( double X );

    // transformed grid attributes
    double _x_0;   // we look for V(X_0) = u(x_0)
    double _x_min; // 0
    double _x_max; // 1
    double _h;     // du
    double _k;     // dt
    int _j;        // nb of h
    int _n;        //    nb of k
    double _u_up;  // up border
    double _u_dw;  // down border

    // V_t = A(X).V_XX + C(X).V_X +  + C(X).V
    double A( double x );
    double B( double x );
    double C( double x );

    // u_t = a(x).u_xx + b(x).u_x + c(x).u
    inline double a( double x );
    inline double b( double x );
    inline double c( double x );

    // u_t = L.u
    //     = L_d(j).u(j-1) + L_m(j).u(j)  + L_u(j).u(j+1)
    inline double L( int i, int j );

    // diagonals
    inline double L_u( int j );
    inline double L_m( int j );
    inline double L_d( int j );

    // U_0.(I + k.theta.L) = U_1.(I - k.theta.L)
    // U_0.T_0             = U_1.T_1
    double _theta;

    // identity matrix
    inline double I( int i, int j );

    // n  : T = I - k * theta * L
    inline double T_0( int i, int j );

    // n+1: T = I + k * theta * L
    inline double T_1( int i, int j );

    //! premium & spot greeks read off a solved grid
    struct GridResult
    {
        double premium = 0;
        double delta = 0;
        double gamma = 0;
    };

    //! discrete-monitoring barrier (knock-out): full domain, knocked-side
    //! boundary held at 0, and the knocked region zeroed only at the scheduled
    //! time steps. Empty _discrete_monitor_steps -> no discrete monitoring.
    bool _barrier_is_up = false;
    double _barrier_level = 0;
    std::set<int> _discrete_monitor_steps;

    //! zero a solved layer in the knocked region (used at each monitoring step)
    void ApplyDiscreteBarrier( la_vector* U );

    //! escrowed-dividend model: future-dividend PV at each grid time step (size
    //! N+1, indexed by step i at year-fraction i*k). The grid diffuses the escrowed
    //! value X, so the observed spot at step i is X + _future_pv[i]; the American
    //! early-exercise test uses that observed spot (matching the MCL engine). Empty
    //! when the underlying carries no discrete dividends.
    vector<double> _future_pv;

    //! observed spot at grid step i for an escrowed grid value X (adds the
    //! future-dividend PV; == X when there are no discrete dividends)
    double ObservedSpot( double X, int Step ) const
    {
        return ( Step >= 0 && Step < (int)_future_pv.size() ) ? X + _future_pv[Step] : X;
    }

    //! init & solve. ApplyBarrier turns on barrier handling: a Dirichlet clamp
    //! for continuous monitoring, or the discrete-step zeroing set up here.
    void InitGrid( Contract*, bool ApplyBarrier );
    GridResult SolveGrid( Contract* );

    //! Heston stochastic vol : a 2-D (S, v) Douglas-ADI finite-difference solve
    //! (cross term explicit, S- and v-sweeps implicit). European + American.
    //! Only plain vanillas on a mono Heston underlying are routed here. With Bates
    //! jumps (jump_intensity > 0) it adds the explicit PIDE jump term (IMEX).
    bool UnderlyingIsHeston( Contract* Ctr );
    GridResult SolveHestonGrid( Contract* Ctr );

    //! variance swap : the fair variance is E[ integral sigma^2 dt ], solved on the
    //! 1-D spot grid as the expected accumulated variance (a backward PDE with a
    //! local-variance source, no discount), then PV = notional * DF * (fair - strike).
    //! _variance_mode drops the reaction term (C = 0) while reusing the grid solve.
    bool _variance_mode = false;
    void SolveVarianceSwap( VarianceSwap* Ctr );

    ///////////////////////////////////////////////////////////////////////////////////////////////

  protected:
    void PreCheck() override; //!< require a PDE solution + a pde_configuration
    void PriceBook() override;
    //! price one contract (vanilla, knock-out, or knock-in via in/out parity)
    void PriceContract( Contract* Ctr ) override; //!< single-contract grid solve
    bool GreeksPerContract() const override { return true; }
    bool GridSpotGreeks() const override { return true; } //!< grid yields dV/dS directly

  public:
    //! constructor, destructor
    PricerPDE( const string& ObjectName,
               YamlConfig& YamlConfig );
    ~PricerPDE() override;
};
