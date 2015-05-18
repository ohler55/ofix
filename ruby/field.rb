# encoding: UTF-8

require 'enum'

class Field

  attr_reader :name
  attr_reader :tag

  def initialize(xe)
    @tag = xe.attributes['number'].to_i
    @name = xe.attributes['name']
    @type = xe.attributes['type']
    @where = 'Body'
    @desc = xe.attributes['description']
    @related = 0
    @enums = nil
    if 0 < xe.nodes.size
      @enums = []
      xe.nodes.each { |n|
        next unless n.is_a?(Ox::Element) && n.name == 'value'
        @enums << Enum.new(n)
      }
    end

  end

  def gen_c(f)
    f.write(%|    { #{@tag}, #{ofix_type(@type)}, OFIX_#{@where}, #{@related}, #{@tag.to_s.size + 1}, "#{@tag}=", "#{@name}" },
|)
  end

  def ofix_type(t)
    oft = {
      'AMT' => 'OFIX_Amt',
      'BOOLEAN' => 'OFIX_Boolean',
      'CHAR' => 'OFIX_Char',
      'COUNTRY' => 'OFIX_Country',
      'CURRENCY' => 'OFIX_Currency',
      'DATA' => 'OFIX_Data',
      'DATE' => 'OFIX_UTCDateOnly',
      'DAYOFMONTH' => 'OFIX_DayOfMonth',
      'EXCHANGE' => 'OFIX_Exchange',
      'FLOAT' => 'OFIX_Float',
      'LENGTH' => 'OFIX_Length',
      'LOCALMKTDATE' => 'OFIX_LocalMktDate',
      'MONTHYEAR' => 'OFIX_MonthYear',
      'MULTIPLEVALUESTRING' => 'OFIX_MultipleValueString',
      'NUMINGROUP' => 'OFIX_NumInGroup',
      'PERCENTAGE' => 'OFIX_Percentage',
      'PRICE' => 'OFIX_Price',
      'PRICEOFFSET' => 'OFIX_PriceOffset',
      'QTY' => 'OFIX_Qty',
      'SEQNUM' => 'OFIX_SeqNum',
      'STRING' => 'OFIX_String',
      'TIME' => 'OFIX_Time',
      'UTCDATE' => 'OFIX_UTCDateOnly',
      'UTCDATEONLY' => 'OFIX_UTCDateOnly',
      'UTCTIMEONLY' => 'OFIX_UTCTimeOnly',
      'UTCTIMESTAMP' => 'OFIX_UTCTimestamp',
      'INT' => 'OFIX_Int',
    }[t]
    raise Exception.new("#{t} is not a valid tag type") if oft.nil?
    oft
  end

end # Field
