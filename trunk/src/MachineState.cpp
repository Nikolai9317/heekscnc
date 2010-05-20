// MachineState.cpp
/*
 * Copyright (c) 2009, Dan Heeks
 * This program is released under the BSD license. See the file COPYING for
 * details.
 */

#include "stdafx.h"
#include "MachineState.h"
#include "CuttingTool.h"
#include "CNCPoint.h"


class CFixture;

CMachineState::CMachineState() : m_fixture(NULL, CFixture::G54)
{
        m_location = CNCPoint(0.0, 0.0, 0.0);
        m_cutting_tool_number = 0;  // No tool assigned.
        m_fixture_has_been_set = false;
}

CMachineState::~CMachineState() { }

CMachineState::CMachineState(CMachineState & rhs) : m_fixture(rhs.Fixture())
{
    *this = rhs;  // Call the assignment operator
}

CMachineState & CMachineState::operator= ( CMachineState & rhs )
{
    if (this != &rhs)
    {
        m_location = rhs.Location();
        m_fixture = rhs.Fixture();
        m_cutting_tool_number = rhs.CuttingTool();
        m_fixture_has_been_set = rhs.m_fixture_has_been_set;
    }

    return(*this);
}

bool CMachineState::operator== ( const CMachineState & rhs ) const
{
    if (m_fixture != rhs.Fixture()) return(false);
    if (m_cutting_tool_number != rhs.m_cutting_tool_number) return(false);

    // Don't include the location in the state check.  Moving around the machine is nothing to reset ourselves
    // over.

    // Don't worry about m_fixture_has_been_set

    return(true);
}

/**
    The machine's cutting tool has changed.  Issue the appropriate GCode if necessary.
 */
Python CMachineState::CuttingTool( const int new_cutting_tool )
{
    Python python;

    if (m_cutting_tool_number != new_cutting_tool)
    {
        m_cutting_tool_number = new_cutting_tool;

        // Select the right tool.
        CCuttingTool *pCuttingTool = (CCuttingTool *) CCuttingTool::Find(new_cutting_tool);
        if (pCuttingTool != NULL)
        {
            python << _T("comment(") << PythonString(_T("tool change to ") + pCuttingTool->m_title) << _T(")\n");
            python << _T("tool_change( id=") << new_cutting_tool << _T(")\n");
        } // End if - then
    }

    return(python);
}


/**
    If the machine is changing fixtures, we may need to move up to a safety height before moving on to the
    next fixture.  If it is, indeed, a different fixture then issue the appropriate GCode to make the switch.  This
    routine should not add the GCode unless it's necessary.  We want the AppendTextToProgram() methods to be
    able to call this routine repeatedly without worrying about unnecessary movements.

	If we're moving between two different fixtures, move above the new fixture's touch-off point before
	continuing on with the other machine operations.  This ensures that the cutting tool is somewhere above
	the new fixture before we start any other movements.
 */
Python CMachineState::Fixture( CFixture new_fixture )
{
    Python python;

    if ((m_fixture != new_fixture) || (! m_fixture_has_been_set))
    {
        // The fixture has been changed.  Move to the highest safety-height between the two fixtures.
        if (m_fixture.m_params.m_safety_height_defined)
        {
			if (new_fixture.m_params.m_safety_height_defined)
			{
				wxString comment;
				comment << _("Moving to a safety height common to both ") << m_fixture.m_coordinate_system_number << _(" and ") << new_fixture.m_coordinate_system_number;
				python << _T("comment(") << PythonString(comment) << _T(")\n");

				// Both fixtures have a safety height defined.  Move the highest of the two.
				if (m_fixture.m_params.m_safety_height > new_fixture.m_params.m_safety_height)
				{
					python << _T("rapid(z=") << m_fixture.m_params.m_safety_height / theApp.m_program->m_units << _T(", machine_coordinates='True')\n");
				} // End if - then
				else
				{
					python << _T("rapid(z=") << new_fixture.m_params.m_safety_height / theApp.m_program->m_units << _T(", machine_coordinates='True')\n");
				} // End if - else
			} // End if - then
			else
			{
				// The old fixture has a safety height but the new one doesn't
				python << _T("rapid(z=") << m_fixture.m_params.m_safety_height / theApp.m_program->m_units << _T(", machine_coordinates='True')\n");
			} // End if - else
        }

		// Invoke new coordinate system.
		python << new_fixture.AppendTextToProgram();

		if (m_fixture_has_been_set == true)
		{
			// We must be moving between fixtures rather than an initial fixture setup.

			// Now move to above the touch-off point so that, when we plunge down, we won't hit the old fixture.
			if (new_fixture.m_params.m_touch_off_description.Length() > 0)
			{
				python << _T("comment(") << PythonString(new_fixture.m_params.m_touch_off_description) << _T(")\n");
			}

			if (new_fixture.m_params.m_touch_off_point_defined)
			{
				wxString comment;
				comment << _("Move above touch-off point for ") << new_fixture.m_coordinate_system_number;
				python << _T("comment(") << PythonString(comment) << _T(")\n");
				python << _T("rapid(x=") << new_fixture.m_params.m_touch_off_point.X()/theApp.m_program->m_units << _T(", y=") << new_fixture.m_params.m_touch_off_point.Y()/theApp.m_program->m_units << _T(")\n");
			}
		} // End if - then

		m_fixture_has_been_set = true;
    }

    m_fixture = new_fixture;
    return(python);
}

/**
	Look to see if this object has been handled for this fixture already.
 */
bool CMachineState::AlreadyProcessed( const int object_type, const unsigned int object_id, const CFixture fixture )
{
    Instance instance;
    instance.Type(object_type);
    instance.Id(object_id);
    instance.Fixture(fixture);

    return(m_already_processed.find(instance) != m_already_processed.end());
}

/**
	Remember which objects have been processed for which fixtures.
 */
void CMachineState::MarkAsProcessed( const int object_type, const unsigned int object_id, const CFixture fixture )
{
    Instance instance;
    instance.Type(object_type);
    instance.Id(object_id);
    instance.Fixture(fixture);

	m_already_processed.insert( instance );
}

CMachineState::Instance::Instance( const CMachineState::Instance & rhs ) : m_fixture(NULL, CFixture::G54)
{
    *this = rhs;
}

CMachineState::Instance & CMachineState::Instance::operator= ( const CMachineState::Instance & rhs )
{
    if (this != &rhs)
    {
        m_object_id = rhs.m_object_id;
        m_object_type = rhs.m_object_type;
        m_fixture = rhs.m_fixture;
    }

    return(*this);
}

bool CMachineState::Instance::operator== ( const CMachineState::Instance & rhs ) const
{
    if (m_object_id != rhs.m_object_id) return(false);
    if (m_object_type != rhs.m_object_type) return(false);
    if (m_fixture != rhs.m_fixture) return(false);

    return(true);
}

bool CMachineState::Instance::operator< ( const CMachineState::Instance & rhs ) const
{
    if (m_object_type < rhs.m_object_type) return(true);
    if (m_object_type > rhs.m_object_type) return(false);

    if (m_object_id < rhs.m_object_id) return(true);
    if (m_object_id > rhs.m_object_id) return(false);

    return(m_fixture < rhs.m_fixture);
}
