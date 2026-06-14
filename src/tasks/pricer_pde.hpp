#pragma once
#include "pricer.hpp"

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
    double s;      // spot
    double v;      // vol
    double r;      // carry / drift rate (underlying ccy, quanto-corrected)
    double r_disc; // discount rate (premium ccy); equals r for non-quanto
    date maturity;
    bool is_american;

    // outputs
    double Price;
    double GetGridPrice( double x, gsl_vector* Uy );

    // original grid
    double X_0; // we look for V(X_0) = u(x_0)
    double X_max;
    double X_min;
    double T_max;
    double V_up; // up border
    double V_dw; // dw border

    // transformation settings
    double c1;
    double c2;
    double aa; // alpha
    double bb; // = c1 - c2
    double cc; // = c2

    // transformation function derivatives & inverse : X = Phi(x), x = Psi(X)
    inline double Phi( double x );
    inline double Phi_x( double x );
    inline double Phi_xx( double x );
    inline double Psi( double X );

    // transformed grid attributes
    double x_0;   // we look for V(X_0) = u(x_0)
    double x_min; // 0
    double x_max; // 1
    double h;     // du
    double k;     // dt
    int J;        // nb of h
    int N;        //    nb of k
    double u_up;  // up border
    double u_dw;  // down border

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
    double theta;

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
    void ApplyDiscreteBarrier( gsl_vector* U );

    //! init & solve. ApplyBarrier turns on barrier handling: a Dirichlet clamp
    //! for continuous monitoring, or the discrete-step zeroing set up here.
    void InitGrid( Contract*, bool ApplyBarrier );
    GridResult SolveGrid( Contract* );

    //! price one contract (vanilla, knock-out, or knock-in via in/out parity)
    void PriceContract( Contract* );

    //! Heston stochastic vol : a 2-D (S, v) Douglas-ADI finite-difference solve
    //! (cross term explicit, S- and v-sweeps implicit). European + American.
    //! Only plain vanillas on a mono Heston underlying are routed here.
    bool UnderlyingIsHeston_( Contract* Ctr );
    GridResult SolveHestonGrid( Contract* Ctr );

    ///////////////////////////////////////////////////////////////////////////////////////////////

  protected:
    void PreCheck_() override; //!< require a PDE solution + a pde_configuration
    void PriceBook_() override;
    void PriceContract_( Contract* Ctr ) override;     //!< single-contract grid solve
    bool GreeksPerContract_() const override { return true; }

  public:
    //! constructor, destructor
    PricerPDE( const string& ObjectName,
               YamlConfig& YamlConfig );
    ~PricerPDE() override;
};
