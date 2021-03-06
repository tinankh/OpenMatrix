/**
* @file MatrixDisplay.cpp
* @date November 2016
* Copyright (C) 2016-2018 Altair Engineering, Inc.  
* This file is part of the OpenMatrix Language (�OpenMatrix�) software.
* Open Source License Information:
* OpenMatrix is free software. You can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
* OpenMatrix is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
* You should have received a copy of the GNU Affero General Public License along with this program.  If not, see <http://www.gnu.org/licenses/>.
* 
* Commercial License Information: 
* For a copy of the commercial license terms and conditions, contact the Altair Legal Department at Legal@altair.com and in the subject line, use the following wording: Request for Commercial License Terms for OpenMatrix.
* Altair�s dual-license business model allows companies, individuals, and organizations to create proprietary derivative works of OpenMatrix and distribute them - whether embedded or bundled with other software - under a commercial license agreement.
* Use of Altair�s trademarks and logos is subject to Altair's trademark licensing policies.  To request a copy, email Legal@altair.com and in the subject line, enter: Request copy of trademark and logo usage policy.
*/

// Begin defines/includes
#include "MatrixDisplay.h"

#include <cassert>
#include <iomanip>
#include <sstream>

#include <boost/math/special_functions/sign.hpp>

#include "BuiltInFuncsUtils.h"
#include "Currency.h"
#include "Interpreter.h"
#include "OutputFormat.h"

#include <hwMatrix.h>
#include "math/kernel/GeneralFuncs.h"

// End defines/includes

//------------------------------------------------------------------------------
// Constructor - Only currency is allowed to construct
//------------------------------------------------------------------------------
MatrixDisplay::MatrixDisplay(const Currency& cur) 
    : CurrencyDisplay (cur)
    , _uppercase      (false)
    , _precision      (OutputFormat::PRECISION_SHORT)
    , _displayformat  (DisplayFormatInt)
    , _haslargeint    (false)
    , _formatinteger  (0)
    , _formatdecimal  (0)
    , _maxint         (static_cast<long long>  (1e+12))
    , _maxfloat       (static_cast<long double>(1e+6))
    , _minfloat       (static_cast<long double>(1e-6))
    , _maxdigits      (static_cast<long long>(1e+15))
    , _totaldigits    (12)
    , _delimiter      ("  ")
{
}
//------------------------------------------------------------------------------
// Gets output
//------------------------------------------------------------------------------
std::string MatrixDisplay::GetOutput(const OutputFormat* fmt,
                                     std::ostringstream& os) const
{
    if (m_mode == DISPLAYMODE_QUIT || m_mode == DISPLAYMODE_EXIT)
    {
        const_cast<MatrixDisplay*>(this)->ResetFormat();
		return "";   // Do nothing
    }

    // Print header
    int numrows = 0;
    int numcols = 0;
    GetCurrencySize(numrows, numcols);
    os << "[Matrix] " << numrows << " x " << numcols;
    std::string header (os.str());

    if (numrows == 0 || numcols == 0)
    {
        return header;
    }

    if (!IsHeaderPrinted())
    {
        header = "";
    }

	bool paginate = IsValidDisplaySize();
	if (!paginate)    
    {
		std::string output (GetOutputNoPagination(fmt));       
        return (header + output);
    }
    
    // Scan the matrix first and get the column widths
    const_cast<MatrixDisplay*>(this)->Initialize(fmt, NULL, NULL); 
    if (!m_initialized) return "";

    m_linesPrinted ++;  // Matrix header will be printed

	if (m_mode == DISPLAYMODE_FORWARD || m_mode == DISPLAYMODE_RIGHT ||
		m_mode == DISPLAYMODE_DOWN)
    {
        std::string data (GetOutputForwardPagination(fmt));
        if (!data.empty())
        {
            return header + data;
        }
        else
        {
            return "";
        }
    }        
    else if (m_mode == DISPLAYMODE_BACK || m_mode == DISPLAYMODE_LEFT ||
	         m_mode == DISPLAYMODE_UP)
    {
		std::string data (GetOutputBackPagination(fmt));
        if (!data.empty())
        {
            return header + data;
        }
        else
        {
            return "";
        }
    }        

    return header;
}
//------------------------------------------------------------------------------
// Gets matrix data with no pagination - using defaults
//------------------------------------------------------------------------------
std::string MatrixDisplay::GetOutputNoPagination(const OutputFormat* fmt) const
{
    return GetOutputNoPagination(fmt, "\n", true, -1);
}
//------------------------------------------------------------------------------
// Gets matrix data with no pagination
//------------------------------------------------------------------------------
std::string MatrixDisplay::GetOutputNoPagination(const OutputFormat* fmt,
                                                 const std::string&  rowdelim,
                                                 bool                startwithnewline,
                                                 int                 coffset) const
{
    const hwMatrix* datamtx = m_currency.Matrix();
    if (!datamtx) return "";

	int  numrows    = datamtx->M();
    int  numcols    = datamtx->N();
    bool isReal     = datamtx->IsReal();
    bool isrealdata = datamtx->IsRealData();

    std::string output ("");

    if (!m_initialized)
    {
        const_cast<MatrixDisplay*>(this)->ResetFormat();
        const_cast<MatrixDisplay*>(this)->Initialize(fmt, NULL);
    }

    if (!m_initialized)
        return "";

    if (_realwidth.empty() || _realwidth.size() != numcols)
        return ""; // Initialization could have been interrupted

    // Get column widths for pretty outputs
    if (!isrealdata)
    {
        if (_imagwidth.empty() || _imagwidth.size() != numcols)
            return ""; // Initialization could have been interrupted
    }

    std::string myindent = GetIndentString(m_indent);
	for (int i = 0; i < numrows; ++i)
	{	
        if (startwithnewline || i > 0)
	        output += rowdelim;  

        if (i < coffset)
        {
            for (int j = 0; j < numcols; ++j)
                output += _delimiter;
        }

        output += myindent;
		for (int j = 0; j < numcols; ++j)
		{
            int iwidth = isrealdata ? 0 : _imagwidth[j];
            GetOutput(i, j, _realwidth[j], iwidth, isReal, isrealdata, output);

            if (j != numcols -1)
                output += _delimiter;  // Delimiter between columns
        }
	}
    const_cast<MatrixDisplay*>(this)->ResetFormat();
	return output;
}
//------------------------------------------------------------------------------
// Gets matrix data with forward pagination
//------------------------------------------------------------------------------
std::string MatrixDisplay::GetOutputForwardPagination(const OutputFormat* fmt) const
{
    const hwMatrix* datamtx = m_currency.Matrix();
    assert(datamtx);
    
	int  numrows    = datamtx->M();
    int  numcols    = datamtx->N();
    bool isReal     = datamtx->IsReal();
    bool isrealdata = datamtx->IsRealData();

    assert(_realwidth.size() == numcols);
    if (!isrealdata)
    {
        assert(_imagwidth.size() == numcols);
    }

    std::string header;
    std::string data;
    std::string output;

    int i        = 0;
    int j        = 0;
    int maxlines = std::max(GetNumRowsToFit()-1, 1); // Add pagination msg
    if (m_linesPrinted >= maxlines)
    {
         m_linesPrinted = maxlines - 1;  // Force print 1 line
    }
         
    bool firstheader     = true;
    bool linesprinted    = false;
    bool skipFirstHeader = (!IsHeaderPrinted());
    if (skipFirstHeader)
    {
        m_deleteLine = true;
    }

    int rbegin = m_rowBegin;
    int cbegin = m_colBegin;
    int rend   = m_rowEnd;
    int cend   = m_colEnd;

    std::string myindent = GetIndentString(m_indent);
    while (m_linesPrinted < maxlines && (i < numrows || j < numcols))
    {
        bool wasPaginating = WasPaginating();

	    int startrow  = (m_rowBegin >= 0) ? m_rowBegin : 0;
	    int startcol  = (m_colBegin >= 0) ? m_colBegin : 0;			
        int maxcols   = GetNumColumnsToFit(startcol, numcols, true, isrealdata);

	    int totalrows = std::min(numrows, startrow + maxlines-m_linesPrinted);
        int totalcols = std::min(numcols, startcol + maxcols);

        for (i = startrow; i < totalrows; ++i)
        {
            data += myindent;
            for (j = startcol; j < totalcols; ++j)
		    {
                int iwidth = isrealdata ? 0 : _imagwidth[j];
                GetOutput(i, j, _realwidth[j], iwidth, isReal, isrealdata, data);

                if (j != maxcols -1)
                {
                    data += _delimiter;
                }
            }
            data += "\n";
            m_linesPrinted++;
            linesprinted = true;
        }
        m_rowEnd = std::max(totalrows-1, 0);
        m_colEnd = std::max(totalcols-1, 0);

        if (!skipFirstHeader)
        {
            if (wasPaginating || IsPaginating())
            {
                if (!wasPaginating || firstheader)
                {
                    header += " ";
                }
                else
                {
                    header += "\n";
                }
                header += GetPaginationHeader(numrows, numcols);
                firstheader = false;
            }
            else if (!wasPaginating)
            {
                header += "\n";
            }
        }
        else
        {
            skipFirstHeader = false;
        }

        output += header + data;
        m_linesPrinted ++;
        linesprinted = true;

        int cachedLines = m_linesPrinted + 1;
        if (cachedLines < maxlines && IsPaginating())
        {
            const_cast<MatrixDisplay*>(this)->SetForwardDisplayData();
            m_linesPrinted = cachedLines;
        }
        else if (cachedLines >= maxlines && !firstheader)
        {
            break;
        }

        header = "";
        data   = "";
    }
    StripEndline(output);

    if (!linesprinted)
    {
        m_rowBegin = rbegin;
        m_rowEnd   = rend;
        m_colBegin = cbegin;
        m_colEnd   = cend;
    }
    return output;
}
//------------------------------------------------------------------------------
// Gets pagination info for printing
//------------------------------------------------------------------------------
std::string MatrixDisplay::GetPaginationHeader(int rows, int cols) const
{
    std::string msg;
    if (m_rowBegin >= 0 || m_rowEnd + 1 < rows)
    {
        int lastrow = std::min(m_rowEnd + 1, rows);
	    std::ostringstream os; 
        if (m_rowBegin + 1 != lastrow)
            os << "Rows[" << m_rowBegin + 1 << ":" << lastrow   << "]";
		else
            os << "Row[" << lastrow << "]";
        msg = os.str();
    }

    if (m_colBegin >= 0 || m_colEnd + 1 < cols)
    {
        int lastcol = std::min(m_colEnd + 1, cols);
        std::ostringstream os; 
        if (m_colBegin + 1 != lastcol)
            os << "Columns[" << m_colBegin + 1 << ":" << lastcol << "]";
        else
            os << "Column[" <<  lastcol << "]";
        if (!msg.empty())
        {
            msg += " ";
        }
        msg += os.str();
    }

    if (!msg.empty())
    {
        msg += "\n";
    }
	return msg;
}
//------------------------------------------------------------------------------
// Gets matrix data with back pagination
//------------------------------------------------------------------------------
std::string MatrixDisplay::GetOutputBackPagination(const OutputFormat* fmt) const
{
    const hwMatrix* datamtx = m_currency.Matrix();
    assert (datamtx);

	int  numrows    = datamtx->M();
    int  numcols    = datamtx->N();
    bool isReal     = datamtx->IsReal();
    bool isrealdata = datamtx->IsRealData();

    assert(_realwidth.size() == numcols);
    if (!isrealdata)
    {
        assert(_imagwidth.size() == numcols);
    }

    std::string header;
    std::string data;
    std::string output;

    int i        = 0;
    int j        = 0;
    int maxlines = std::max(GetNumRowsToFit()-1, 1); // Add pagination msg
    if (m_linesPrinted >= maxlines)
    {
         m_linesPrinted = maxlines - 1;  // Force print 1 line
    }

    bool firstheader  = true;
    bool linesprinted = false;

    int rbegin = m_rowBegin;
    int cbegin = m_colBegin;
    int rend   = m_rowEnd;
    int cend   = m_colEnd;

    std::string myindent    = GetIndentString(m_indent);

    while (m_linesPrinted < maxlines && (i < numrows || j < numcols))
    {
        bool wasPaginating = WasPaginating();

	    int endrow = (m_rowEnd > 0 && m_rowEnd < numrows) ? m_rowEnd : numrows - 1;
	    int endcol = (m_colEnd > 0 && m_colEnd < numcols) ? m_colEnd : numcols - 1;

        int maxcols = GetNumColumnsToFit(0, numcols, true, isrealdata);
        int startrow = std::max(0, endrow+1 - maxlines+m_linesPrinted);
        int startcol = std::max(0, endcol+1 - maxcols);

        for (i = startrow; i < numrows && i <= endrow; ++i)
        {
            data += myindent;
		    for (j = startcol; j < numcols && j <= endcol; ++j)
		    {
                int iwidth = isrealdata ? 0 : _imagwidth[j];
                GetOutput(i, j, _realwidth[j], iwidth, isReal, isrealdata, data);

                if (j != maxcols -1)
                {
                    data += _delimiter;
                }
                linesprinted = true;
            }
            data += "\n";
            m_linesPrinted++;
        }
        m_rowBegin = startrow;
        m_colBegin = startcol;

        if (wasPaginating || IsPaginating())
        {          
            if (!wasPaginating || firstheader)
            {
                header += " ";
            }
            else
            {
                 header += "\n";
            }
            firstheader = false;
            header += GetPaginationHeader(numrows, numcols);
        }
        else if (!wasPaginating)
        {
            header = "\n";
        }

        if (linesprinted)
        {
            output += header + data;
        }
        m_linesPrinted ++;
        linesprinted = true;

        int cachedLines = m_linesPrinted+1;
        if (cachedLines < maxlines && IsPaginating())
        {
            const_cast<MatrixDisplay*>(this)->SetBackDisplayData();
            m_linesPrinted = cachedLines;
        }
        else if (cachedLines >= maxlines && !firstheader)
        {
            break;
        }

        header = "";
        data   = "";
    }
    StripEndline(output);
    if (!linesprinted)
    {
        m_rowBegin = rbegin;
        m_rowEnd   = rend;
        m_colBegin = cbegin;
        m_colEnd   = cend;
    }

    return output;
}
//------------------------------------------------------------------------------
// Converts real value to formatted string
//------------------------------------------------------------------------------
std::string MatrixDisplay::RealToString(double val, DisplayFormat fmt) const
{    
    if (IsNegInf_T (val)) return "-Inf";
    if (IsInf_T(val))     return "Inf";
    if (IsNaN_T(val))     return "NaN";
    
    if (fmt == DisplayFormatInt)
    {
        std::ostringstream os;
        os.setf(static_cast<std::ios_base::fmtflags>(0), std::ios::floatfield);       
        os << std::setprecision(static_cast<std::streamsize>(_totaldigits));

        if (boost::math::signbit(val)) // Detect -0
            os << "-" << fabs(val);
        else
            os << val;
        return std::string (os.str());
    }

    char tmp[1024];
    
    if (fmt == DisplayFormatFloat && _precision == OutputFormat::PRECISION_SHORT)
    {
        sprintf(tmp, "%.5f", val);
        return std::string (tmp);
    }
    else if (fmt == DisplayFormatFloat && _precision == OutputFormat::PRECISION_LONG)
        sprintf(tmp, "%.8f", val);

    else if (fmt == DisplayFormatFloat && _precision == OutputFormat::PRECISION_SCALAR)
    {
        std::ostringstream os;
        os << "%" << _formatinteger << "." << _formatdecimal << "f";
        sprintf(tmp, std::string(os.str()).c_str(), val);
    }

    else if (fmt == DisplayFormatScientific && _precision == OutputFormat::PRECISION_SHORT)
        _uppercase ? sprintf(tmp, "%.5E", val) : sprintf(tmp, "%.5e", val);

    else if (fmt == DisplayFormatScientific && _precision == OutputFormat::PRECISION_LONG)
        _uppercase ? sprintf(tmp, "%.8E", val) : sprintf(tmp, "%.8e", val);

    else
    {
        std::ostringstream os;
        os << "%" << _formatinteger << "." << _formatdecimal;
        if (_uppercase)
            os << "E";
        else
            os << "e";
        sprintf(tmp, std::string(os.str()).c_str(), val);
    }
    return std::string (tmp);
}
//------------------------------------------------------------------------------
// Sets data for forward display
//------------------------------------------------------------------------------
void MatrixDisplay::SetForwardDisplayData()
{
	if (IsPaginatingCols()) // Col pagination	
		m_colBegin = m_colEnd + 1;

	else if (IsPaginatingRows()) // Row pagination
	{
        m_colBegin = 0;    // Start row pagination
		m_rowBegin = m_rowEnd + 1;
	}
    // Make sure indices are positive
    m_rowBegin = std::max(m_rowBegin, 0);
    m_colBegin = std::max(m_colBegin, 0);

    // Reset the end indices
    m_colEnd = 0;
    m_rowEnd = 0;

    // Figure out how many lines have been printed and set it
    //if (!m_parentDisplay ||                // First currency being paginated
   // if (m_rowBegin > 0 || m_colBegin > 0) // Only this currency is being paginated
        m_linesPrinted = 0; //m_linesPrinted = 1;
}
//------------------------------------------------------------------------------
// Sets indices for back pagination
//------------------------------------------------------------------------------
void MatrixDisplay::SetBackDisplayData()
{
    m_linesPrinted = 0;     // Reset number of lines printed

	// We have reached the beginning of the matrix, so we cannot go back further
	if (m_rowBegin == 0 && m_colBegin == 0) return;

    const hwMatrix* mtx = m_currency.Matrix();
    int numcols = mtx ? mtx->N() : 0;

	if (m_colEnd > 0 && numcols > 0 && m_colEnd + 1 <= numcols)
		m_colEnd = m_colBegin - 1;
	
	if (m_colBegin <= 0)
	{
		m_rowEnd = m_rowBegin - 1;
		m_colEnd = numcols - 1; // Start row pagination
	}
}
//------------------------------------------------------------------------------
// Sets indices for right pagination
//------------------------------------------------------------------------------
void MatrixDisplay::SetRightDisplayData()
{
    m_linesPrinted = 0;     // Reset number of lines printed
	if (IsPaginatingCols()) // Col pagination	
		m_colBegin = m_colEnd + 1;
}
//------------------------------------------------------------------------------
// Sets indices for left pagination
//------------------------------------------------------------------------------
void MatrixDisplay::SetLeftDisplayData()
{
    m_linesPrinted = 0;     // Reset number of lines printed
	// We have reached the beginning of the matrix, so we cannot go back further
	if (m_colBegin == 0) return;

    const hwMatrix* mtx = m_currency.Matrix();
    int numcols = mtx ? mtx->N() : 0;

	if (m_colEnd > 0 && numcols > 0 && m_colEnd + 1 <= numcols)
		m_colEnd = m_colBegin - 1;	
}
//------------------------------------------------------------------------------
// Resets format
//------------------------------------------------------------------------------
void MatrixDisplay::ResetFormat()
{
    _displayformat = DisplayFormatInt;
    _precision     = OutputFormat::PRECISION_SHORT;
    _haslargeint   = false;
    _uppercase     = false;
    _formatinteger = 0;
    _formatdecimal = 0;

    _maxint    = static_cast<long long>(1e+12);
    _maxfloat  = static_cast<long double>(1e+6);
    _minfloat  = static_cast<long double>(1e-6);
    _maxdigits = static_cast<long long>(1e+15);
    _totaldigits = 12;

    _realwidth.clear();
    _imagwidth.clear();
}
//------------------------------------------------------------------------------
// Gets output
//------------------------------------------------------------------------------
void MatrixDisplay::GetOutput(int          row,
                              int          col,
                              int          realwidth,
                              int          imagwidth,
                              bool         isreal,
                              bool         isrealdata,
                              std::string& output) const
{
    const hwMatrix* datamtx = m_currency.Matrix();
    assert(datamtx);

    double      realval = 0.0;
    double      imagval = 0.0;
    std::string imagsign;
    
    if (isreal)
        realval = (*datamtx)(row, col);
    else
        GetComplexNumberVals(datamtx->z(row, col), realval, imagval, imagsign);

    std::ostringstream os;    
    os << std::right << std::setfill(' ') << std::setw(realwidth);
    os << RealToString(realval);

    output += os.str();
    if (!isrealdata)
    {
        output += imagsign;
        std::ostringstream ios;
        ios << std::right << std::setfill(' ') << std::setw(imagwidth);
        ios << (RealToString(imagval) + "i");
    
        output += ios.str();
    }
}
//------------------------------------------------------------------------------
// Gets number of rows and cols in currency
//------------------------------------------------------------------------------
void MatrixDisplay::GetCurrencySize(int& rows, int& cols) const
{
    const hwMatrix* mtx = m_currency.Matrix();
    if (!mtx) return;

    rows = mtx->M();
    cols = mtx->N();
}
//------------------------------------------------------------------------------
// Gets number of columns to fit
//------------------------------------------------------------------------------
int MatrixDisplay::GetNumColumnsToFit(int  refcol, 
                                      int  numcols, 
                                      bool forward, 
                                      bool isreal) const
{
    int signwidth  = (!isreal) ? 3 : 0;

    int linewidth = 0;
    int colstofit = 0;
    int colpadsize = static_cast<int>(_delimiter.size());

    std::string myindent = GetIndentString(m_indent);
    if (!myindent.empty())
    {
        linewidth += static_cast<int>(myindent.size());
    }

    if (forward)
    {
        for (int i = refcol; i < numcols; ++i)
        {
            int realwidth = _realwidth[i];
            int imagwidth = (!isreal) ? _imagwidth[i] : 0;

            if (i != refcol)
                linewidth += colpadsize;

            linewidth += realwidth + signwidth + imagwidth;
            if (linewidth > m_maxCols) return colstofit;

            colstofit++;
        }
        return colstofit;
    }

    for (int i = refcol; i >= 0; --i)
    {
        int realwidth = _realwidth[i];
        int imagwidth = (!isreal) ? _imagwidth[i] : 0;

        linewidth += realwidth + signwidth + imagwidth;
        if (i != refcol)
            linewidth += colpadsize;

        if (linewidth > m_maxCols) return colstofit;
        colstofit++;
    }
    return colstofit;
}
//------------------------------------------------------------------------------
// Sets matrix format
//------------------------------------------------------------------------------
void MatrixDisplay::SetFormat(const OutputFormat* fmt, Interpreter* interp)
{
    const hwMatrix* mtx = m_currency.Matrix();
    assert(mtx);
    assert(!mtx->IsEmpty());

    if (fmt)
    {
        _formatinteger = std::min(18, fmt->GetIntegerPart());
        _formatdecimal = std::min(18,  fmt->GetDecimalPart());

        if (_formatinteger > 0 && _formatdecimal > 0)
        {   // Initialization for user specified format
            _totaldigits   = _formatinteger + _formatdecimal;
            _maxdigits     = static_cast<long long>(std::pow(10.0, static_cast<double>(_totaldigits)));
            _maxint        = _maxdigits;
            _maxfloat      = std::pow(10.0, _formatinteger);
            _minfloat      = std::pow(10.0, -(_formatdecimal+1));
            _precision     = OutputFormat::PRECISION_SCALAR;
            
        }
        else if (fmt->GetPrecision() == OutputFormat::PRECISION_LONG) 
        {   // Initialization for format long
            _formatinteger = 0;
            _formatdecimal = 0;
            _totaldigits   = 24;
            _maxdigits     = static_cast<long long>(1e+15);
            _minfloat      = 1e-9;
            _maxint        = static_cast<long long>(1e+12);
            _precision     = OutputFormat::PRECISION_LONG;
        }
        else
        {   // Initialization for short format
            _formatinteger = 0;
            _formatdecimal = 0;
            _totaldigits   = 12;
            _maxdigits     = static_cast<long long>(1e+15);
            _maxint        = static_cast<long long>(1e+12);
            _maxfloat      = 1e+6;
            _minfloat      = 1e-6;
            _precision     = OutputFormat::PRECISION_SHORT;
        }

        if (fmt->GetFlags() == std::ios::scientific)
        {
            _displayformat = DisplayFormatScientific;
            return;
        }

        else if (fmt->GetFlags() == (std::ios::scientific | std::ios::uppercase))
        {
            _displayformat = DisplayFormatScientific;
            _uppercase     = true;
            return;
        }
    }

    // Scan matrix and set format
    int  numcols = mtx->N();
    int  numrows = mtx->M();
    bool isreal  = mtx->IsReal();

    for (int j = 0; j < numcols && _displayformat != DisplayFormatScientific; ++j)
    {
        if (interp && interp->IsInterrupt()) return;

        for (int i = 0; i < numrows && _displayformat != DisplayFormatScientific; ++i)
        {
            DisplayFormat thisformat = isreal ? GetFormat((*mtx)(i, j)) :
                                                GetFormat(mtx->z(i, j));        
            _displayformat = std::max(thisformat, _displayformat);
        }
    }
}
//------------------------------------------------------------------------------
// Gets format for given double
//------------------------------------------------------------------------------
MatrixDisplay::DisplayFormat MatrixDisplay::GetFormat(double val) const
{
    // If we are already in scientific format, we are done
    assert (_displayformat != DisplayFormatScientific); 

    if (IsInf_T(val) || IsNegInf_T (val) || IsNaN_T(val)) return DisplayFormatInt;

    double aval        = fabs(val);

    if (BuiltInFuncsUtils::IsInt(val)) // Integer, compare by values
    {
        // Check for very small floats
        if (IsZero(aval) && !(aval < 1e-16)) return DisplayFormatScientific;

        // Max digits for integers is 12 (short format), 15 (long format)
        if ((_precision == OutputFormat::PRECISION_SHORT  && !(aval < _maxint))    ||
            (_precision == OutputFormat::PRECISION_LONG   && !(aval < _maxdigits)) ||
            (_precision == OutputFormat::PRECISION_SCALAR && !(aval < _maxdigits)))
            return DisplayFormatScientific;

        // Check for large integers
        if (aval > LLONG_MAX) return DisplayFormatScientific;

        // Additional check if we have large integers
        if (!_haslargeint && _precision == OutputFormat::PRECISION_SHORT &&
            !(aval < _maxfloat))
            _haslargeint = true;
        else if (!_haslargeint && _precision == OutputFormat::PRECISION_SCALAR &&
            !(aval < _maxfloat))
            _haslargeint = true;

        std::string tmp (RealToString(val, DisplayFormatInt));
        if (tmp.size() > _totaldigits) return DisplayFormatScientific;

        if (!_haslargeint || _displayformat == DisplayFormatInt) return DisplayFormatInt;
    }

    if (_haslargeint) return DisplayFormatScientific;

    // Additional check if we have very small floats
    if (!(aval > _minfloat)) return DisplayFormatScientific;

    std::string tmp (RealToString(val, DisplayFormatFloat));
    
    if (tmp.size() > _totaldigits) return DisplayFormatScientific;

    return DisplayFormatFloat;
}
//------------------------------------------------------------------------------
// Returns format for complex numbers
//------------------------------------------------------------------------------
MatrixDisplay::DisplayFormat MatrixDisplay::GetFormat(const hwComplex& val) const
{
    DisplayFormat realfmt = GetFormat(val.Real());
    if (realfmt == DisplayFormatScientific) return DisplayFormatScientific;

    DisplayFormat imagfmt = GetFormat(val.Imag());
    if (imagfmt == DisplayFormatScientific) return DisplayFormatScientific;

    return std::max(realfmt, imagfmt);
}
//------------------------------------------------------------------------------
// Initializes
//------------------------------------------------------------------------------
void MatrixDisplay::Initialize(const OutputFormat* fmt, 
                               Interpreter*        interp,
                               CurrencyDisplay*    parent)
{
    if (!m_initialized)
    {
        const hwMatrix* mtx = m_currency.Matrix();
        if (!mtx || mtx->IsEmpty()) return;

        ResetFormat();          // Reset format and widths
        SetFormat(fmt, interp); // Sets the format
        SetWidth(interp);       // Scan matrix and set the width of columns
    }
    
    CurrencyDisplay::Initialize(fmt, interp, parent);
}
//------------------------------------------------------------------------------
// Scan matrix and set the width of different columns
//------------------------------------------------------------------------------
void MatrixDisplay::SetWidth(Interpreter* interp)
{
    const hwMatrix* mtx = m_currency.Matrix();
    assert(mtx);
    assert(!mtx->IsEmpty());

    int numrows     = mtx->M();
    int numcols     = mtx->N();
    bool isreal     = mtx->IsReal();
    bool isrealdata = mtx->IsRealData();

    _realwidth.reserve(numcols);
    if (!isrealdata)
        _imagwidth.reserve(numcols);

    for (int j = 0; j < numcols; ++j)
    {
        int realwidth = 0;
        int imagwidth = 0;

        for (int i = 0; i < numrows; ++i)
        {
            if (interp && interp->IsInterrupt())
            {
                const_cast<MatrixDisplay*>(this)->ResetFormat();
                return;
            }

            double      realval = 0.0;
            double      imagval = 0.0;
            std::string imagsign;
            
            if (isreal)
                realval = (*mtx)(i, j);
            else
                GetComplexNumberVals(mtx->z(i, j), realval, imagval, imagsign);

            std::string realstr (RealToString(realval));
            assert (!realstr.empty());
               
            realwidth = std::max(realwidth, static_cast<int>(realstr.size()));

            if (isrealdata) continue;

            std::string imagstr (RealToString(imagval));
            assert (!imagstr.empty());
            imagwidth = std::max(imagwidth, static_cast<int>(imagstr.size()) + 1);
        }
        _realwidth.push_back(realwidth);
        if (!isrealdata)
            _imagwidth.push_back(imagwidth);
    }
}
//------------------------------------------------------------------------------
// Sets the delimiter
//------------------------------------------------------------------------------
void MatrixDisplay::SetDelimiter(const std::string& val)
{
    if (!val.empty())
        _delimiter = val;
    else
        _delimiter = "  ";
}
//------------------------------------------------------------------------------
// Utility which returns matrix values as string
//------------------------------------------------------------------------------
std::string MatrixDisplay::GetOutputValues(const Currency&     in,
                                           const OutputFormat* fmt,
                                           const std::string&  rdelim,
                                           const std::string&  cdelim,
                                           const std::string&  precisionreal,
                                           const std::string&  precisionimag,
                                           int                 coffset)
{    
    // 1x1 matrices are treated as scalars or complex numbers
    bool isscalar  = in.IsScalar();
    bool iscomplex = in.IsComplex();
    bool ismatrix  = in.IsMatrix();
    if (!isscalar && !iscomplex && !ismatrix) return "";

    if ( (isscalar || iscomplex) && precisionreal.empty())
    {
        Currency tmp(in);
        tmp.DispOutput();
        return tmp.GetOutputString(fmt);
    }
    else if (isscalar)
    {
        char tmp[1024];
        sprintf(tmp, precisionreal.c_str(), in.Scalar());
        std::string output(tmp);
        return tmp;
    }
    else if (iscomplex)
    {
        double      rval = 0.0;
        double      ival = 0.0;
        std::string isign;
        GetComplexNumberVals(in.Complex(), rval, ival, isign);
        std::string output = RealToString(rval, precisionreal) + isign;
                             RealToString(ival, precisionimag) + "i";
        return output;
    }
    else if (!precisionreal.empty())  // Just get the values without format
    {
        std::string     output;
        const hwMatrix* mtx   = in.Matrix();
        int             nrows = mtx ? mtx->M() : 0;
        int             ncols = mtx ? mtx->N() : 0;
        if (!mtx || nrows == 0 || ncols == 0) return "";

        bool isreal     = mtx->IsReal();
        bool isrealdata = mtx->IsRealData();

        for (int i = 0; i < nrows; ++i)
        {
            for (int j = 0; j < ncols; ++j)
            {
                if (j < coffset)
                    output += cdelim;

                if (isreal)
                    output += RealToString((*mtx)(i, j), precisionreal);
                else
                {
                    double      rval = 0.0;
                    double      ival = 0.0;
                    std::string isign;
                    GetComplexNumberVals(mtx->z(i, j), rval, ival, isign);
                    output += RealToString(rval, precisionreal);
                    if (!isrealdata)
                        output += isign + RealToString(ival, precisionimag) + "i";
                }
                if ( j < ncols - 1)
                    output += cdelim;
            }
            output += rdelim;
        }
        return output;
    }

    Currency cur(in);
    MatrixDisplay* display = static_cast<MatrixDisplay*>(in.GetDisplay());
    if (!display) return "";

    display->SetDelimiter(cdelim);
    
    std::string output = display->GetOutputNoPagination(fmt, rdelim, false, coffset);
    return output;
}
//------------------------------------------------------------------------------
// Utility which returns real value to string
//------------------------------------------------------------------------------
std::string MatrixDisplay::RealToString(double val, const std::string& precision)
{    
    if (IsNegInf_T (val)) return "-Inf";
    if (IsInf_T(val))     return "Inf";
    if (IsNaN_T(val))     return "NaN";

    std::string fmt (precision);
    if (fmt.empty())
        fmt = "%lf";

    bool isint = BuiltInFuncsUtils::IsInt(val);
    if (!isint)
    {
        size_t pos = fmt.find("d");
        if (pos != std::string::npos)
            fmt.replace(pos, 2, "lf"); // For sprintf invalid formats
    }

    // Handle double precision issues
    if (!isint)
    {
        if (fmt.find("lf") == std::string::npos)
        {
            size_t pos = fmt.find("g");
            if (pos == std::string::npos)
                pos = fmt.find("f");
            if (pos != std::string::npos)
                fmt.replace(pos, 2, "lf"); // For sprintf invalid formats
        }
    }
    else
    {
        size_t pos1 = fmt.find("d");
        if (pos1 != std::string::npos)
        {
            size_t pos = fmt.find(".");
            if (pos != std::string::npos)
            {
                fmt.replace(pos1, 2, "lf"); // For sprintf invalid formats
            }
        }
    }
    char tmp[1028];
    sprintf(tmp, fmt.c_str(), val);
    std::string out(tmp);
    return tmp;
}
//------------------------------------------------------------------------------
// Gets values as a string
//------------------------------------------------------------------------------
std::string MatrixDisplay::GetValues(const OutputFormat* fmt) const
{
    _delimiter = " ";

    std::string out = GetOutputNoPagination(fmt, ";", false, -1);
    return out;
}
//------------------------------------------------------------------------------
// Returns true if matrix was paginating
//------------------------------------------------------------------------------
bool MatrixDisplay::WasPaginating() const
{
    bool wasPaginating = (IsPaginating()); 
    if (wasPaginating && m_colBegin == 0 && m_colEnd == 0 && m_rowBegin == 0 && 
        m_rowEnd   == 0)
    {
        wasPaginating = false;
    }
    return wasPaginating;
}
//------------------------------------------------------------------------------
// Returns true if end of pagination message needs to be printed
//------------------------------------------------------------------------------
bool MatrixDisplay::GetPaginationEndMsg(std::string& msg) const
{
    if (m_parentDisplay)
    {
        return false;
    }
    return CurrencyDisplay::GetPaginationEndMsg(msg);
}
//------------------------------------------------------------------------------
// Returns true if paginating
//------------------------------------------------------------------------------
bool MatrixDisplay::IsPaginating() const
{
    return (IsPaginatingRows() || IsPaginatingCols());
}
//------------------------------------------------------------------------------
// True if header needs to be printed
//------------------------------------------------------------------------------
bool MatrixDisplay::IsHeaderPrinted() const
{
    if (m_parentDisplay && m_parentDisplay->IsNDMatrixDisplay() && 
        WasPaginating())
    {
        return false;
    }
    return true;
}

// End of file
